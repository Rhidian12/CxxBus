// MIT License
//
// Copyright (c) 2026 Rhidian De Wit
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "DBusConnection.h"

#include <algorithm>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <exception>
#include <format>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <tuple>

#include "DBus.h"
#include "DBusHelpers.h"
#include "DBusMatchRule.h"
#include "DBusMessage.h"
#include "DBusNameCache.h"
#include "DBusTypes.h"
#include "IncomingDBusMessage.h"
#include "Log.h"
#include "SyncDBusConnection.h"

namespace cxxbus
{
  using namespace std::chrono_literals;

  namespace
  {
#ifndef CXX_BUS_LOGLEVEL
#define CXX_BUS_LOGLEVEL Error
#endif  // CXX_BUS_LOGLEVEL

#ifndef CXX_BUS_EXIT_IF_EXPIRED
#define CXX_BUS_EXIT_IF_EXPIRED(var) \
  if ((var).expired())               \
  {                                  \
    co_return;                       \
  }
#endif  // CXX_BUS_EXIT_IF_EXPIRED

    Logger const LOGGER{.logLevel = LogLevel::CXX_BUS_LOGLEVEL};
  }  // namespace

  boost::asio::awaitable<std::shared_ptr<DBusConnection>> DBusConnection::Create(
      boost::asio::io_context& ioService, std::optional<DBusWellKnownName> wellKnownName, BusType busType)
  {
    std::shared_ptr<DBusConnection> conn{new DBusConnection(ioService, std::move(wellKnownName))};

    co_await conn->Connect(busType);

    co_return conn;
  }

  std::shared_ptr<DBusConnection> DBusConnection::CreateDetached(
      boost::asio::io_context& ioService, std::optional<DBusWellKnownName> wellKnownName,
      std::function<boost::asio::awaitable<void>()> onConnectedCallback, BusType busType)
  {
    std::shared_ptr<DBusConnection> conn{new DBusConnection(ioService, std::move(wellKnownName))};

    boost::asio::co_spawn(ioService, conn->Connect(busType),
                          [&ioService, cb = std::move(onConnectedCallback)](std::exception_ptr e)
                          {
                            if (e)
                            {
                              std::rethrow_exception(e);
                            }

                            if (cb)
                            {
                              boost::asio::co_spawn(ioService, cb(), boost::asio::detached);
                            }
                          });
    return conn;
  }

  DBusConnection::DBusConnection(boost::asio::io_context& ioService, std::optional<DBusWellKnownName> wellKnownName)
    : m_state(new InternalState{
          .replyChannels =
              std::map<uint32_t,
                       boost::asio::experimental::channel<void(boost::system::error_code, IncomingDBusMessage)>*>{},
          .onIncomingSignal = {},
          .onDisconnected = {},
          .sendLoop =
              boost::asio::experimental::channel<void(
                  boost::system::error_code,
                  std::tuple<DBusMessage, uint32_t,
                             std::shared_ptr<boost::asio::experimental::channel<void(boost::system::error_code)>>>)>{
                  ioService, 10},
          .connectionReady = false,
          .connectionCompleted = boost::asio::experimental::channel<void(boost::system::error_code)>{ioService},
          .nrOfWaiters = 0,
          .socket = std::make_shared<boost::asio::local::stream_protocol::socket>(ioService),
          .uniqueConnection = nullptr,
          .wellKnownNames = std::make_shared<std::vector<DBusWellKnownName>>(),
          .serial = std::make_shared<uint32_t>(1),
          .subscriptionCounter = std::make_shared<uint32_t>(0),
          .matchRules = std::make_shared<std::unordered_map<uint32_t, MatchRuleInfo>>(),
          .nameCache = std::make_shared<DBusNameCache>(*this),
          .objectPathHandlers =
              std::make_shared<std::unordered_map<std::string, AwaitableSignal<void, IncomingDBusMessage>>>(),
          .unhandledIncomingMessages = std::make_shared<std::queue<IncomingDBusMessage>>(),
          .timer = boost::asio::system_timer{ioService},
          .shouldQuit = false})
  {
    if (wellKnownName.has_value())
    {
      m_state->wellKnownNames->push_back(*wellKnownName);
    }
  }

  DBusConnection::~DBusConnection()
  {
    // We do this check because preferably the user used `Close()` to already kill the connection
    if (m_state->socket->is_open())
    {
      CloseData();
    }
  }

  void DBusConnection::CloseData()
  {
    LOGGER.LogTrace("Closing channels and signals");
    m_state->sendLoop.close();
    m_state->onIncomingSignal.disconnect_all_slots();
    m_state->objectPathHandlers->clear();
    m_state->shouldQuit = true;
    m_state->timer.cancel();
    m_state->connectionReady = false;

    LOGGER.LogTrace("Closing shared socket");
    if (m_state->socket->is_open())
    {
      boost::system::error_code ec;
      std::ignore = m_state->socket->close(ec);
    }
    LOGGER.LogTrace("Closed socket");
  }

  void DBusConnection::HandleConnectionLost()
  {
    if (m_state->shouldQuit)
    {
      return;
    }

    LOGGER.LogError("Connection to the dbus-daemon was lost unexpectedly");

    m_state->connectionReady = false;
    m_state->shouldQuit = true;
    m_state->timer.cancel();
    m_state->sendLoop.close();

    if (m_state->socket->is_open())
    {
      boost::system::error_code ec;
      std::ignore = m_state->socket->close(ec);
    }

    m_state->onDisconnected();
  }

  boost::asio::awaitable<void> DBusConnection::Close()
  {
    LOGGER.LogInfo("Closing DBus Connection");

    auto rules{*m_state->matchRules};
    for (auto const& [id, ruleInfo] : rules)
    {
      co_await RemoveMatchRule(ruleInfo.rule);
    }

    auto names{*m_state->wellKnownNames};
    // Release our well-known name from the dbus-daemon
    LOGGER.LogTrace("Releasing our well-known name");
    for (DBusWellKnownName name : *m_state->wellKnownNames)
    {
      co_await ReleaseWellKnownName(name);
    }

    CloseData();
  }

  boost::asio::awaitable<void> DBusConnection::AuthenticateDBusConnection()
  {
    std::weak_ptr<DBusConnection> weakThis{shared_from_this()};
    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    auto state = m_state;

    // First send a single '\0' byte
    co_await state->socket->async_send(boost::asio::buffer("\0", 1), boost::asio::use_awaitable);
    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    // Next we must authenticate ourselves, we use the EXTERNAL
    // authentication method
    co_await state->socket->async_send(
        boost::asio::buffer(std::format("AUTH EXTERNAL {}\r\n", HexEncodeString(std::to_string(::getuid())))),
        boost::asio::use_awaitable);
    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    // Now we expect to see OK <guid>
    std::string reply{};
    co_await boost::asio::async_read_until(*state->socket, boost::asio::dynamic_buffer(reply), "\r\n",
                                           boost::asio::use_awaitable);
    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    if (!reply.starts_with("OK"))
    {
      LOGGER.LogError("Authentication failed!");
      throw std::runtime_error{"Authentication failed!"};
    }

    // Yippee! All worked, so now start our DBus Connection!
    co_await state->socket->async_send(boost::asio::buffer("BEGIN\r\n", 7), boost::asio::use_awaitable);
  }

  boost::asio::awaitable<void> DBusConnection::Connect(BusType busType)
  {
    std::weak_ptr<DBusConnection> weakThis{shared_from_this()};
    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    auto state = m_state;

    // Connect to DBus daemon
    if (busType == BusType::SESSION)
    {
      boost::asio::local::stream_protocol::endpoint endpoint{ParseDBusAddress(busType)};
      co_await state->socket->async_connect(endpoint, boost::asio::as_tuple(boost::asio::use_awaitable));
    }
    else
    {
      std::string address = ParseDBusAddress(busType);
      if (address.empty())
      {
        address = "/var/run/dbus/system_bus_socket";
      }
      boost::asio::local::stream_protocol::endpoint endpoint{address};
      co_await state->socket->async_connect(endpoint, boost::asio::as_tuple(boost::asio::use_awaitable));
    }

    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    co_await AuthenticateDBusConnection();

    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    LOGGER.LogTrace("Connected to DBus-daemon. Starting Send loop");
    boost::asio::co_spawn(m_state->socket->get_executor(), SendLoop(), boost::asio::detached);

    LOGGER.LogTrace("Send loop started. Starting Read loop");
    boost::asio::co_spawn(m_state->socket->get_executor(), ReadLoop(), boost::asio::detached);

    boost::asio::co_spawn(m_state->socket->get_executor(), HandleUnhandledIncomingMessages(), boost::asio::detached);
    LOGGER.LogTrace("Read loop started. Starting connection handshake");

    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    // Get our unique bus name
    std::optional<IncomingDBusMessage> reply =
        co_await SendMessageInternal(DBusMessage::Method("Hello")
                                         .Path(ObjectPath{"/org/freedesktop/DBus"})
                                         .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                         .Destination("org.freedesktop.DBus"));
    if (reply.has_value())
    {
      m_state->uniqueConnection = std::make_shared<DBusUniqueConnectionName>(reply->Get<std::string>());
    }

    LOGGER.LogInfo(std::format("Unique Connection ID: {}", m_state->uniqueConnection->GetName()));

    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    // Now, request a well-known name from the dbus-daemon
    auto wellKnownNames{*m_state->wellKnownNames};
    for (DBusWellKnownName name : wellKnownNames)
    {
      reply = co_await SendMessageInternal(
          DBusMessage::Method("RequestName")
              .Path(ObjectPath{"/org/freedesktop/DBus"})
              .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
              .Destination("org.freedesktop.DBus")
              .Parameter(MultipleCompleteTypes<std::string, uint32_t>{name.GetName(), static_cast<uint32_t>(0x1)}));

      if (!reply.has_value())
      {
        LOGGER.LogFatal(
            "Internal error: RequestName() should not be able to return without having received a "
            "reply");
        throw InternalError{
            "Internal error: RequestName() should not be able to return without having received a "
            "reply"};
      }

      switch (reply->Get<uint32_t>())
      {
        case 1:
          LOGGER.LogDebug(std::format("Successfully acquired well-known name '{}'", name.GetName()));
          break;
        // [TODO]: Allow user passing flags for the Well-known name.
        case 2:
          LOGGER.LogError(
              std::format("Well-known name '{}' is already owned by another connection and we did "
                          "not ask to replace the name",
                          name.GetName()));
          break;
        case 3:
          LOGGER.LogError(std::format("The well-known name '{}' already has an owner", name.GetName()));
          break;
        case 4:
          LOGGER.LogDebug("We're already owner of our well-known name");
          break;
        default:
          LOGGER.LogError(std::format("Unknown return value from 'RequestName()': {}", reply->Get<uint32_t>()));
          break;
      }
    }

    LOGGER.LogTrace("Connection handshake completed.");

    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    m_state->connectionReady = true;
    if (m_state->nrOfWaiters > 0)
    {
      co_await m_state->connectionCompleted.async_send(boost::system::error_code{}, boost::asio::use_awaitable);
    }

    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    LOGGER.LogTrace("Subscribing to NameOwnerChanged signal");
    co_await m_state->nameCache->SubscribeToNameChanges();
  }

  boost::asio::awaitable<void> DBusConnection::SendLoop()
  {
    std::weak_ptr<DBusConnection> weakThis{shared_from_this()};
    if (weakThis.expired())
    {
      co_return;
    }

    auto state = m_state;

    while (!weakThis.expired())
    {
      try
      {
        // Wait for an incoming message to send
        auto [message, serial, messageSentChannel] = co_await state->sendLoop.async_receive(boost::asio::use_awaitable);
        co_await boost::asio::async_write(*state->socket, boost::asio::buffer(message.Serialize(serial)),
                                          boost::asio::use_awaitable);

        LOGGER.LogTrace(std::format("Sent message '{}'", message.GetInfo()));
        co_await messageSentChannel->async_send(boost::system::error_code{}, boost::asio::use_awaitable);
      }
      catch (boost::system::system_error const& ex)
      {
        if (state->shouldQuit)
        {
          break;
        }

        if (ex.code().category().name() == std::string{"asio.channel"} && ex.code().value() == 1)
        {
          // Channel closed, exit the loop
          break;
        }
        else if (ex.code().category().name() == std::string{"system"} && ex.code().value() == 125)
        {
          // Operation cancelled. Exit the loop
          break;
        }

        // Any other socket error (e.g. EOF, connection reset, broken pipe) means the connection to the dbus-daemon was
        // lost unexpectedly. Report it and handle the connection loss.
        LOGGER.LogError(std::format("Send loop lost connection to dbus-daemon: {}", ex.what()));
        HandleConnectionLost();
        break;
      }
      catch (std::exception const& ex)
      {
        LOGGER.LogError(std::format("Error occured in message send loop: {}", ex.what()));
      }
    }
  }

  boost::asio::awaitable<void> DBusConnection::ReadLoop()
  {
    std::weak_ptr<DBusConnection> weakThis{shared_from_this()};
    if (weakThis.expired())
    {
      co_return;
    }

    std::vector<byte> rawFullReply{};
    auto state = m_state;

    while (!weakThis.expired())
    {
      try
      {
        rawFullReply.clear();

        std::vector<byte> tempBuffer{};
        tempBuffer.resize(FIRST_HEADER_PART_SIZE);
        co_await boost::asio::async_read(*state->socket, boost::asio::buffer(tempBuffer), boost::asio::use_awaitable);
#if __cpp_lib_containers_ranges
        rawFullReply.append_range(tempBuffer);
#else
        rawFullReply.insert(rawFullReply.end(), tempBuffer.begin(), tempBuffer.end());
#endif
        DBusMessageHeader messageHeader{std::move(tempBuffer)};

        tempBuffer.resize(sizeof(uint32_t));
        co_await boost::asio::async_read(*state->socket, boost::asio::buffer(tempBuffer), boost::asio::use_awaitable);
#if __cpp_lib_containers_ranges
        rawFullReply.append_range(tempBuffer);
#else
        rawFullReply.insert(rawFullReply.end(), tempBuffer.begin(), tempBuffer.end());
#endif
        messageHeader.ParseHeaderFieldLength(std::move(tempBuffer));

        tempBuffer.resize(messageHeader.GetHeaderFieldsLength());
        co_await boost::asio::async_read(*state->socket, boost::asio::buffer(tempBuffer), boost::asio::use_awaitable);
#if __cpp_lib_containers_ranges
        rawFullReply.append_range(std::move(tempBuffer));
#else
        rawFullReply.insert(rawFullReply.end(), tempBuffer.begin(), tempBuffer.end());
#endif

        uint32_t arrPointer{FIRST_HEADER_PART_SIZE};
        messageHeader.ParseRemainderOfHeader(rawFullReply, arrPointer);

        uint32_t const oldArrPointer{arrPointer};
        AddPaddingToSize(arrPointer, DBUS_MESSAGE_BODY_ALIGNMENT);
        uint32_t const nrOfPaddingBytes{arrPointer - oldArrPointer};

        tempBuffer.resize(nrOfPaddingBytes + messageHeader.GetMessageLength());
        co_await boost::asio::async_read(*state->socket, boost::asio::buffer(tempBuffer), boost::asio::use_awaitable);

        // Skip over the padding, we don't care about it
#if __cpp_lib_ranges_to_container
        IncomingDBusMessage message{std::move(messageHeader),
                                    std::ranges::to<std::vector>(tempBuffer | std::views::drop(nrOfPaddingBytes))};
#else
        IncomingDBusMessage message{std::move(messageHeader),
                                    std::vector<byte>(tempBuffer.begin() + nrOfPaddingBytes, tempBuffer.end())};
#endif

        co_await HandleReadMessage(std::move(message));
      }
      catch (boost::system::system_error const& ex)
      {
        if (state->shouldQuit)
        {
          break;
        }

        if (ex.code().category().name() == std::string{"asio.channel"} && ex.code().value() == 1)
        {
          // Channel closed, exit the loop
          break;
        }
        else if (ex.code().category().name() == std::string{"system"} && ex.code().value() == 125)
        {
          // Operation cancelled. Exit the loop
          break;
        }

        // Any other socket error (e.g. EOF, connection reset, broken pipe) means the connection to the dbus-daemon was
        // lost unexpectedly. Report it and handle the connection loss.
        LOGGER.LogError(std::format("Read loop lost connection to dbus-daemon: {}", ex.what()));
        HandleConnectionLost();
        break;
      }
      catch (std::exception const& ex)
      {
        LOGGER.LogError(std::format("Error occured in message read loop: {}", ex.what()));
      }
    }
  }

  boost::asio::awaitable<void> DBusConnection::HandleReadMessage(IncomingDBusMessage message)
  {
    std::weak_ptr<DBusConnection> weakThis{shared_from_this()};
    if (weakThis.expired())
    {
      co_return;
    }

    auto state = m_state;

    // We're dealing with a reply from a previously sent message
    if (message.GetHeader().GetReplySerial().has_value())
    {
      uint32_t const replySerial{message.GetHeader().GetReplySerial().value()};
      LOGGER.LogTrace(std::format("Received reply to message with serial '{}'. Reply: '{}'", replySerial, message.GetInfo()));

      if (!state->replyChannels.contains(replySerial))
      {
        // It should not be possible to get a reply to a message we don't know
        LOGGER.LogFatal(
            std::format("Received a reply with serial '{}' but we do not have the serial of "
                        "the original message",
                        replySerial));
        throw InternalError{"Internal error: Receiving reply to a message, but the serial is unknown to us"};
      }

      co_await state->replyChannels[replySerial]->async_send(boost::system::error_code{}, std::move(message),
                                                             boost::asio::use_awaitable);
    }
    // Simply an incoming message
    else
    {
      LOGGER.LogTrace(std::format("Received incoming message '{}'", message.GetInfo()));

      if (message.GetHeader().GetMessageType() == DBusMessageType::SIGNAL)
      {
        LOGGER.LogTrace("Incoming message is signal, checking match rules");

        for (MatchRuleInfo const& info : *state->matchRules | std::views::values)
        {
          if (info.rule.Matches(message,
                                state->nameCache->GetWellKnownNames(message.GetHeader().GetSender().value_or(""))))
          {
            LOGGER.LogTrace(std::format("Rule '{}' matched incoming signal", info.rule.GetRule()));
            co_await (*info.callback)(message);
          }
        }
      }
      else if (message.GetHeader().GetObjectPath().has_value() &&
               state->objectPathHandlers->contains(
                   message.GetHeader()
                       .GetObjectPath()
                       .transform([](ObjectPath const& path) { return std::string{path}; })
                       .value()))
      {
        co_await (*state->objectPathHandlers)[message.GetHeader().GetObjectPath().value().GetPath()](
            std::move(message));
      }
      else
      {
        // If it has no reply serial, then it means it's an incoming call
        if (!state->onIncomingSignal.empty())
        {
          co_await state->onIncomingSignal(message);
        }
      }
    }
  }

  boost::asio::awaitable<void> DBusConnection::HandleUnhandledIncomingMessages()
  {
    std::weak_ptr<DBusConnection> weakThis{shared_from_this()};
    if (weakThis.expired())
    {
      co_return;
    }

    auto state = m_state;

    while (!weakThis.expired() || !state->shouldQuit)
    {
      try
      {
        if (state->unhandledIncomingMessages->empty())
        {
          state->timer.expires_after(1s);
          co_await state->timer.async_wait(boost::asio::use_awaitable);
          continue;
        }

        IncomingDBusMessage message{state->unhandledIncomingMessages->front()};
        state->unhandledIncomingMessages->pop();

        co_await HandleReadMessage(std::move(message));
      }
      catch (boost::system::system_error const& ex)
      {
        if (state->shouldQuit)
        {
          break;
        }

        if (ex.code().category().name() == std::string{"system"} && ex.code().value() == 125)
        {
          // Operation cancelled. Exit the loop
          break;
        }

        // Any other socket error (e.g. EOF, connection reset, broken pipe) means the connection to the dbus-daemon was
        // lost unexpectedly. Report it and handle the connection loss.
        LOGGER.LogError(std::format("Unhandled message loop lost connection to dbus-daemon: {}", ex.what()));
        HandleConnectionLost();
        break;
      }
      catch (std::exception const& ex)
      {
        LOGGER.LogError(std::format("Error occured in unhandled message loop: {}", ex.what()));
      }
    }
  }

  boost::asio::awaitable<std::optional<IncomingDBusMessage>> DBusConnection::SendMessageInternal(DBusMessage message)
  {
    // 1st, if we're expecting a reply, store a channel so we can await a reply from the dbus-daemon
    boost::asio::experimental::channel<void(boost::system::error_code, IncomingDBusMessage)> replyChannel{
        m_state->socket->get_executor(), 1};

    bool const expectsReply{!std::ranges::contains(message.GetFlags(), DBusMessageFlags::NO_REPLY_EXPECTED)};

    if (expectsReply)
    {
      m_state->replyChannels[*m_state->serial] = &replyChannel;
    }

    std::shared_ptr<boost::asio::experimental::channel<void(boost::system::error_code)>> messageSentChannel =
        std::make_shared<boost::asio::experimental::channel<void(boost::system::error_code)>>(
            m_state->socket->get_executor(), 1);

    // 2nd, send our message to the SendLoop() coroutine to actually send the message
    co_await m_state->sendLoop.async_send(boost::system::error_code{},
                                          std::make_tuple(std::move(message), (*m_state->serial)++, messageSentChannel),
                                          boost::asio::use_awaitable);

    // 3rd, wait for the message to be sent.
    co_await messageSentChannel->async_receive(boost::asio::use_awaitable);

    // 4th, check if we're expecting a reply
    if (!expectsReply)
    {
      co_return std::nullopt;
    }

    // 5th, wait for the reply to be sent back to us from the ReadLoop() coroutine
    IncomingDBusMessage reply = co_await replyChannel.async_receive(boost::asio::use_awaitable);
    m_state->replyChannels.erase(reply.GetHeader().GetReplySerial().value());

    if (reply.GetHeader().GetMessageType() == DBusMessageType::ERROR)
    {
      // We got an error, so throw an error here
      if (!reply.GetHeader().GetErrorName().has_value())
      {
        LOGGER.LogFatal("Incoming DBus Error did not specify the ERROR_NAME header field");
      }

      throw DBusError{
          reply.GetHeader().GetErrorName().has_value() ? reply.GetHeader().GetErrorName().value() : "Missing",
          reply.HasArguments() && reply.GetHeader().GetSignature().value_or(Signature{""}) == "s"
              ? reply.Get<std::string>()
              : "No error message was provided by the remote"};
    }

    co_return reply;
  }

  boost::asio::awaitable<IncomingDBusMessage> DBusConnection::SendMessage(DBusMessage message)
  {
    // Wait until our Connnection is ready
    if (!m_state->connectionReady)
    {
      LOGGER.LogTrace("Connection not ready yet, waiting for it to complete");
      m_state->nrOfWaiters++;
      co_await m_state->connectionCompleted.async_receive(boost::asio::use_awaitable);
    }

    std::optional<IncomingDBusMessage> reply = co_await SendMessageInternal(std::move(message));
    if (!reply.has_value())
    {
      LOGGER.LogFatal(std::format("SendMessage() should not be able to return without having received a reply"));
      throw InternalError{
          "Internal Error: SendMessage() should not be able to return without having received a "
          "reply"};
    }

    co_return reply.value();
  }

  boost::asio::awaitable<void> DBusConnection::SendMessageNoReply(DBusMessage message)
  {
    // Wait until our Connnection is ready
    if (!m_state->connectionReady)
    {
      LOGGER.LogTrace("Connection not ready yet, waiting for it to complete");
      m_state->nrOfWaiters++;
      co_await m_state->connectionCompleted.async_receive(boost::asio::use_awaitable);
    }

    // Let's auto add the NO_REPLY_EXPECTED flag if it's not been added
    if (!std::ranges::contains(message.GetFlags(), DBusMessageFlags::NO_REPLY_EXPECTED))
    {
      message.Flag(DBusMessageFlags::NO_REPLY_EXPECTED);
    }

    std::ignore = co_await SendMessageInternal(std::move(message));

    co_return;
  }

  boost::asio::awaitable<void> DBusConnection::AddMatchRule(
      DBusMatchRule rule, std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback)
  {
    LOGGER.LogTrace(std::format("Adding match rule '{}'", rule.GetRule()));

    co_await SendMessage(DBusMessage::Method("AddMatch")
                             .Path(ObjectPath{"/org/freedesktop/DBus"})
                             .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                             .Destination("org.freedesktop.DBus")
                             .Parameter(rule.GetRule()));

    std::shared_ptr<AwaitableSignal<void, IncomingDBusMessage>> signal =
        std::make_shared<AwaitableSignal<void, IncomingDBusMessage>>();
    signal->connect(
        [cb = std::move(callback)](IncomingDBusMessage message) -> std::function<boost::asio::awaitable<void>()>
        {
          return [cb, message = std::move(message)](this auto&&) -> boost::asio::awaitable<void>
          // This cannot be a lambda because the lambda would get destroyed, causing `cb` and `message` to go
          // out-of-scope
          { return InvokeAsyncCallback(cb, std::move(message)); };
        });

    m_state->matchRules->emplace((*m_state->subscriptionCounter)++,
                                 MatchRuleInfo{.rule = std::move(rule), .callback = std::move(signal)});

    co_return;
  }

  boost::asio::awaitable<void> DBusConnection::RemoveMatchRule(DBusMatchRule rule)
  {
    LOGGER.LogTrace(std::format("Removing match rule '{}'", rule.GetRule()));

    co_await SendMessage(DBusMessage::Method("RemoveMatch")
                             .Path(ObjectPath{"/org/freedesktop/DBus"})
                             .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                             .Destination("org.freedesktop.DBus")
                             .Parameter(rule.GetRule()));

    auto const it = std::ranges::find_if(*m_state->matchRules, [&rule](std::pair<uint32_t, MatchRuleInfo> const& elem)
                                         { return elem.second.rule == rule; });
    m_state->matchRules->erase(it);

    co_return;
  }

  void DBusConnection::RegisterObjectPathHandler(
      ObjectPath path, std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback)
  {
    (*m_state->objectPathHandlers)[path.GetPath()].connect(
        [cb = std::move(callback)](IncomingDBusMessage message) -> std::function<boost::asio::awaitable<void>()>
        {
          return [cb, message = std::move(message)](this auto&&) -> boost::asio::awaitable<void>
          // This cannot be a lambda because the lambda would get destroyed, causing `cb` and `message` to go
          // out-of-scope
          { return InvokeAsyncCallback(cb, std::move(message)); };
        });
  }

  void DBusConnection::UnregisterObjectPathHandler(ObjectPath path)
  {
    (*m_state->objectPathHandlers).erase(path.GetPath());
  }

  boost::asio::awaitable<void> DBusConnection::RequestWellKnownName(DBusWellKnownName name)
  {
    if (std::ranges::contains(*m_state->wellKnownNames, name))
    {
      throw std::runtime_error{std::format("This connection already owns the name '{}'", name.GetName())};
    }

    auto reply = co_await SendMessage(
        DBusMessage::Method("RequestName")
            .Path(ObjectPath{"/org/freedesktop/DBus"})
            .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
            .Destination("org.freedesktop.DBus")
            .Parameter(MultipleCompleteTypes<std::string, uint32_t>{name.GetName(), static_cast<uint32_t>(0x1)}));

    switch (reply.Get<uint32_t>())
    {
      case 1:
        LOGGER.LogDebug(std::format("Successfully acquired well-known name '{}'", name.GetName()));
        break;
      // [TODO]: Allow user passing flags for the Well-known name.
      case 2:
        LOGGER.LogError(
            std::format("Well-known name '{}' is already owned by another connection and we did "
                        "not ask to replace the name",
                        name.GetName()));
        break;
      case 3:
        LOGGER.LogError(std::format("The well-known name '{}' already has an owner", name.GetName()));
        break;
      case 4:
        LOGGER.LogDebug("We're already owner of our well-known name");
        break;
      default:
        LOGGER.LogError(std::format("Unknown return value from 'RequestName()': {}", reply.Get<uint32_t>()));
        break;
    }

    m_state->wellKnownNames->push_back(name);
    co_return;
  }

  boost::asio::awaitable<void> DBusConnection::ReleaseWellKnownName(DBusWellKnownName name)
  {
    if (!std::ranges::contains(*m_state->wellKnownNames, name))
    {
      throw std::runtime_error{std::format("This connection does not own the name '{}'", name.GetName())};
    }

    LOGGER.LogTrace(std::format("Releasing our well-known name '{}'", name.GetName()));
    IncomingDBusMessage const ret = co_await SendMessage(DBusMessage::Method("ReleaseName")
                                                             .Path(ObjectPath{"/org/freedesktop/DBus"})
                                                             .Destination("org.freedesktop.DBus")
                                                             .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                                             .Parameter(name.GetName()));

    switch (ret.Get<uint32_t>())
    {
      case 1:
        LOGGER.LogDebug(std::format("Successfully released well-known name '{}'", name.GetName()));
        break;
      case 2:
        LOGGER.LogError(std::format("Well-known name '{}' is not owned by the dbus-daemon", name.GetName()));
        break;
      case 3:
        LOGGER.LogError(std::format("Well-known name '{}' is not owned by this connection", name.GetName()));
        break;
      default:
        LOGGER.LogError(std::format("Unknown return value from 'ReleaseName()': {}", ret.Get<uint32_t>()));
        break;
    }

    auto it = std::ranges::remove(*m_state->wellKnownNames, name);
    m_state->wellKnownNames->erase(it.begin(), it.end());
    co_return;
  }

  void DBusConnection::ReceiveIncomingMessages(
      std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback)
  {
    m_state->onIncomingSignal.connect(
        [cb = std::move(callback)](IncomingDBusMessage message) -> std::function<boost::asio::awaitable<void>()>
        {
          return [cb, message = std::move(message)](this auto&&) -> boost::asio::awaitable<void>
          // This cannot be a lambda because the lambda would get destroyed, causing `cb` and `message` to go
          // out-of-scope
          { return InvokeAsyncCallback(cb, std::move(message)); };
        });
  }

  std::vector<DBusWellKnownName> const& DBusConnection::GetWellKnownNames() const
  {
    return *m_state->wellKnownNames;
  }

  bool DBusConnection::IsConnected() const
  {
    return m_state->connectionReady;
  }

  boost::signals2::connection DBusConnection::OnDisconnected(std::function<void()> callback)
  {
    return m_state->onDisconnected.connect(std::move(callback));
  }

  void DBusConnection::SimulateConnectionLoss()
  {
    LOGGER.LogTrace("Simulating loss of the connection to the dbus-daemon");

    if (m_state->socket->is_open())
    {
      // Hard shutdown the socket, this should cause the HandleConnectionLoss() function to get called
      boost::system::error_code ec;
      std::ignore = m_state->socket->shutdown(boost::asio::local::stream_protocol::socket::shutdown_both, ec);
    }
  }
}  // namespace cxxbus
