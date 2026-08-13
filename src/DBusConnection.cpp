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
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <cstdint>
#include <exception>
#include <format>
#include <future>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>

#include "AwaitableSignal.h"
#include "DBusHelpers.h"
#include "DBusMatchRule.h"
#include "DBusMessage.h"
#include "DBusNameCache.h"
#include "DBusTypes.h"
#include "IncomingDBusMessage.h"
#include "InvokeAsyncCallback.h"
#include "Log.h"

namespace cxxbus
{
  using namespace std::chrono_literals;

  namespace
  {
#ifndef CXX_BUS_LOGLEVEL
#define CXX_BUS_LOGLEVEL ERROR
#endif  // CXX_BUS_LOGLEVEL

#ifndef CXX_BUS_EXIT_IF_EXPIRED
#define CXX_BUS_EXIT_IF_EXPIRED(var) \
  if ((var).expired())               \
  {                                  \
    co_return;                       \
  }
#endif  // CXX_BUS_EXIT_IF_EXPIRED

    Logger const LOGGER{.logLevel = LogLevel::CXX_BUS_LOGLEVEL};

    void IOThread(std::shared_ptr<boost::asio::io_context> ioContext)
    {
      ioContext->run();
    }

    template <typename T>
    T WaitOnAsyncWork(std::shared_ptr<boost::asio::strand<typename boost::asio::io_context::executor_type>> strand,
                      std::function<boost::asio::awaitable<T>()> work)
    {
      std::promise<T> promise;
      std::future<T> future = promise.get_future();

      if constexpr (!std::is_void_v<T>)
      {
        boost::asio::co_spawn(*strand, work(),
                              [&promise](std::exception_ptr ptr, T res)
                              {
                                if (ptr)
                                {
                                  promise.set_exception(ptr);
                                }
                                else
                                {
                                  promise.set_value(res);
                                }
                              });
      }
      else
      {
        boost::asio::co_spawn(*strand, work(),
                              [&promise](std::exception_ptr ptr)
                              {
                                if (ptr)
                                {
                                  promise.set_exception(ptr);
                                }
                                else
                                {
                                  promise.set_value();
                                }
                              });
      }

      future.wait();

      return future.get();
    }

    boost::asio::awaitable<void> HopToIOContext(boost::asio::io_context& ioContext)
    {
      co_return co_await boost::asio::co_spawn(
          ioContext, []() -> boost::asio::awaitable<void> { co_return; }, boost::asio::use_awaitable);
    }
  }  // namespace

  boost::asio::awaitable<std::shared_ptr<DBusConnection>> DBusConnection::Create(
      boost::asio::io_context& ioService, std::optional<DBusWellKnownName> wellKnownName, BusType busType)
  {
    std::shared_ptr<DBusConnection> conn{new DBusConnection(ioService, std::move(wellKnownName))};

    co_await conn->Connect(busType, ioService);

    co_return conn;
  }

  std::shared_ptr<DBusConnection> DBusConnection::CreateDetached(
      boost::asio::io_context& ioService, std::optional<DBusWellKnownName> wellKnownName,
      std::function<boost::asio::awaitable<void>()> onConnectedCallback, BusType busType)
  {
    std::shared_ptr<DBusConnection> conn{new DBusConnection(ioService, std::move(wellKnownName))};

    boost::asio::co_spawn(ioService, conn->Connect(busType, ioService),
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

  std::shared_ptr<DBusConnection> DBusConnection::CreateSync(boost::asio::io_context& ioService,
                                                             std::optional<DBusWellKnownName> wellKnownName,
                                                             BusType busType)
  {
    std::shared_ptr<DBusConnection> conn{new DBusConnection(ioService, std::move(wellKnownName))};
    WaitOnAsyncWork<void>(conn->m_state->strand, [conn, busType]() -> boost::asio::awaitable<void>
                          { return conn->Connect(busType, *conn->m_state->ioContext); });

    return conn;
  }

  DBusConnection::DBusConnection(boost::asio::io_context& ioService, std::optional<DBusWellKnownName> wellKnownName)
    : m_state()
    , m_userIOContext(ioService)
  {
    std::shared_ptr<boost::asio::io_context> ioContext{std::make_shared<boost::asio::io_context>()};
    m_state =
        std::shared_ptr<InternalState>(new InternalState{
            .ioContext = ioContext,
            .replyChannels =
                std::map<uint32_t,
                         boost::asio::experimental::channel<void(boost::system::error_code, IncomingDBusMessage)>*>{},
            .onIncomingSignal = {},
            .messageFilter = {},
            .messageFilterID = 0,
            .onDisconnected = {},
            .sendLoop =
                boost::asio::experimental::channel<void(
                    boost::system::error_code,
                    std::tuple<DBusMessage, uint32_t,
                               std::shared_ptr<boost::asio::experimental::channel<void(boost::system::error_code)>>>)>{
                    *ioContext, 10},
            .connectionReady = false,
            .connectionCompleted = boost::asio::experimental::channel<void(boost::system::error_code)>{*ioContext},
            .nrOfWaiters = 0,
            .strand = std::make_shared<boost::asio::strand<typename boost::asio::io_context::executor_type>>(
                ioContext->get_executor()),
            .socket = std::make_shared<boost::asio::local::stream_protocol::socket>(*ioContext),
            .uniqueConnection = nullptr,
            .wellKnownNames = std::make_shared<std::vector<DBusWellKnownName>>(),
            .serial = std::make_shared<uint32_t>(1),
            .subscriptionCounter = std::make_shared<uint32_t>(0),
            .matchRules = std::make_shared<std::unordered_map<uint32_t, MatchRuleInfo>>(),
            .nameCache = std::make_shared<DBusNameCache>(*this),
            .objectPathHandlers = std::make_shared<
                std::unordered_map<std::string, std::shared_ptr<AwaitableSignal<void, IncomingDBusMessage>>>>(),
            .mutex = std::make_shared<std::mutex>(),
            .workGuard = nullptr,
            .ioThread = nullptr,
            .unhandledIncomingMessages = std::make_shared<std::queue<IncomingDBusMessage>>(),
            .timer = boost::asio::system_timer{*ioContext},
            .shouldQuit = false});

    if (wellKnownName.has_value())
    {
      m_state->wellKnownNames->push_back(*wellKnownName);
    }

    m_state->workGuard =
        std::make_unique<boost::asio::executor_work_guard<typename boost::asio::io_context::executor_type>>(
            boost::asio::make_work_guard(*ioContext));
    m_state->ioThread = std::make_shared<std::thread>(&IOThread, ioContext);
  }

  DBusConnection::~DBusConnection()
  {
    CloseDataSync();
  }

  boost::asio::awaitable<void> DBusConnection::CloseData()
  {
    LOGGER.LogTrace("Closing data");
    co_await boost::asio::co_spawn(
        *m_state->strand,
        [this]() -> boost::asio::awaitable<void>
        {
          LOGGER.LogTrace("Closing channels and signals");
          m_state->sendLoop.close();
          m_state->onIncomingSignal.disconnect_all_slots();
          m_state->objectPathHandlers->clear();
          m_state->shouldQuit = true;
          m_state->timer.cancel();
          m_state->connectionReady.store(false);

          LOGGER.LogTrace("Closing socket");
          if (m_state->socket->is_open())
          {
            boost::system::error_code ec;
            std::ignore = m_state->socket->close(ec);
          }
          LOGGER.LogTrace("Closed socket");
          co_return;
        },
        boost::asio::use_awaitable);
  }

  void DBusConnection::CloseDataSync()
  {
    if (m_state->strand->running_in_this_thread())
    {
      boost::asio::co_spawn(*m_state->strand, CloseData(), boost::asio::detached);
      LOGGER.LogTrace("Detaching thread");
      m_state->workGuard.reset();
      m_state->ioThread->detach();
      LOGGER.LogTrace("Detached thread");

      return;
    }

    LOGGER.LogTrace("Closing data sync");
    WaitOnAsyncWork<void>(m_state->strand, [this]() { return CloseData(); });

    LOGGER.LogTrace("Joining thread");
    m_state->workGuard.reset();
    m_state->ioThread->join();
    LOGGER.LogTrace("Joined thread");
  }

  boost::asio::awaitable<void> DBusConnection::HandleConnectionLost()
  {
    if (m_state->shouldQuit)
    {
      co_return;
    }

    LOGGER.LogError("Connection to the dbus-daemon was lost unexpectedly");

    m_state->connectionReady.store(false);
    m_state->shouldQuit = true;
    m_state->timer.cancel();
    m_state->sendLoop.close();

    if (m_state->socket->is_open())
    {
      boost::system::error_code ec;
      std::ignore = m_state->socket->close(ec);
    }

    boost::asio::co_spawn(
        m_userIOContext,
        [this]() -> boost::asio::awaitable<void>
        {
          m_state->onDisconnected();
          co_return;
        },
        boost::asio::detached);
  }

  boost::asio::awaitable<void> DBusConnection::Close(boost::asio::io_context& ioContext)
  {
    LOGGER.LogInfo("Closing DBus Connection");

    if (m_state->connectionReady.load())
    {
      auto rules{*m_state->matchRules};
      for (auto const& [id, ruleInfo] : rules)
      {
        co_await RemoveMatchRule(ruleInfo.rule, ioContext);
      }

      auto names{*m_state->wellKnownNames};
      // Release our well-known name from the dbus-daemon
      LOGGER.LogTrace("Releasing our well-known name");
      for (DBusWellKnownName name : names)
      {
        co_await ReleaseWellKnownName(name, ioContext);
      }
    }

    co_await CloseData();
  }

  boost::asio::awaitable<void> DBusConnection::Close()
  {
    co_return co_await Close(m_userIOContext);
  }

  void DBusConnection::CloseSync()
  {
    WaitOnAsyncWork<void>(m_state->strand, [this]() { return Close(*m_state->ioContext); });
  }

  boost::asio::awaitable<void> DBusConnection::AuthenticateDBusConnection()
  {
    std::weak_ptr<DBusConnection> weakThis{shared_from_this()};
    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    auto state = m_state;

    // First send a single '\0' byte
    co_await state->socket->async_send(boost::asio::buffer("\0", 1),
                                       boost::asio::bind_executor(*state->strand, boost::asio::use_awaitable));
    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    // Next we must authenticate ourselves, we use the EXTERNAL
    // authentication method
    std::string const auth = HexEncodeString(std::to_string(::getuid()));
    co_await state->socket->async_send(boost::asio::buffer(std::format("AUTH EXTERNAL {}\r\n", auth)),
                                       boost::asio::bind_executor(*state->strand, boost::asio::use_awaitable));
    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    // Now we expect to see OK <guid>
    std::string reply{};
    co_await boost::asio::async_read_until(*state->socket, boost::asio::dynamic_buffer(reply), "\r\n",
                                           boost::asio::bind_executor(*state->strand, boost::asio::use_awaitable));
    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    if (!reply.starts_with("OK"))
    {
      LOGGER.LogError("Authentication failed!");
      throw std::runtime_error{"Authentication failed!"};
    }

    // Yippee! All worked, so now start our DBus Connection!
    co_await state->socket->async_send(boost::asio::buffer("BEGIN\r\n", 7),
                                       boost::asio::bind_executor(*state->strand, boost::asio::use_awaitable));
  }

  boost::asio::awaitable<void> DBusConnection::Connect(BusType busType, boost::asio::io_context& ioContext)
  {
    std::weak_ptr<DBusConnection> weakThis{shared_from_this()};
    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    auto state = m_state;

    // Connect to DBus daemon
    if (busType == BusType::SESSION)
    {
      boost::asio::local::stream_protocol::endpoint endpoint{ParseDBusAddress(busType)};
      co_await state->socket->async_connect(
          endpoint, boost::asio::bind_executor(*state->strand, boost::asio::as_tuple(boost::asio::use_awaitable)));
      LOGGER.LogTrace("Connected to DBus Session bus");
    }
    else
    {
      std::string address = ParseDBusAddress(busType);
      if (address.empty())
      {
        address = "/var/run/dbus/system_bus_socket";
      }
      boost::asio::local::stream_protocol::endpoint endpoint{address};
      co_await state->socket->async_connect(
          endpoint, boost::asio::bind_executor(*state->strand, boost::asio::as_tuple(boost::asio::use_awaitable)));
      LOGGER.LogTrace("Connected to DBus System bus");
    }

    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    co_await AuthenticateDBusConnection();

    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    LOGGER.LogTrace("Connected to DBus-daemon. Starting Send loop");
    boost::asio::co_spawn(*m_state->strand, SendLoop(), boost::asio::detached);

    LOGGER.LogTrace("Send loop started. Starting Read loop");
    boost::asio::co_spawn(*m_state->strand, ReadLoop(), boost::asio::detached);

    // boost::asio::co_spawn(*m_state->strand, HandleUnhandledIncomingMessages(), boost::asio::detached);
    LOGGER.LogTrace("Read loop started. Starting connection handshake");

    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    // Get our unique bus name
    std::optional<IncomingDBusMessage> reply =
        co_await SendMessageInternal(DBusMessage::Method("Hello")
                                         .Path(ObjectPath{"/org/freedesktop/DBus"})
                                         .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                         .Destination("org.freedesktop.DBus"),
                                     ioContext);
    if (reply.has_value())
    {
      m_state->uniqueConnection = std::make_shared<DBusUniqueConnectionName>(reply->Get<std::string>());
    }

    LOGGER.LogInfo("Unique Connection ID: {}", m_state->uniqueConnection->GetName());

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
              .Parameter(MultipleCompleteTypes<std::string, uint32_t>{name.GetName(), static_cast<uint32_t>(0x1)}),
          ioContext);

      if (!reply.has_value())
      {
        LOGGER.LogFatal(
            "Internal error: RequestName() should not be able to return without having received a "
            "reply");
        throw InternalError{
            "Internal error: RequestName() should not be able to return without having received a "
            "reply"};
      }

      uint32_t const ret = reply->Get<uint32_t>();
      switch (ret)
      {
        case 1:
          LOGGER.LogDebug("Successfully acquired well-known name '{}'", name.GetName());
          break;
        // [TODO]: Allow user passing flags for the Well-known name.
        case 2:
          LOGGER.LogError(
              "Well-known name '{}' is already owned by another connection and we did "
              "not ask to replace the name",
              name.GetName());
          break;
        case 3:
          LOGGER.LogError("The well-known name '{}' already has an owner", name.GetName());
          break;
        case 4:
          LOGGER.LogDebug("We're already owner of our well-known name");
          break;
        default:
          LOGGER.LogError("Unknown return value from 'RequestName()': {}", ret);
          break;
      }
    }

    LOGGER.LogTrace("Connection handshake completed.");

    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    m_state->connectionReady.store(true);
    if (m_state->nrOfWaiters > 0)
    {
      co_await m_state->connectionCompleted.async_send(
          boost::system::error_code{}, boost::asio::bind_executor(*m_state->strand, boost::asio::use_awaitable));
    }

    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    LOGGER.LogTrace("Subscribing to NameOwnerChanged signal");
    co_await m_state->nameCache->SubscribeToNameChanges(ioContext);
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
      LOGGER.LogTrace("Received reply to message with serial '{}'. Reply: '{}'", replySerial, message.GetInfo());

      if (!state->replyChannels.contains(replySerial))
      {
        // It should not be possible to get a reply to a message we don't know
        LOGGER.LogFatal(
            "Received a reply with serial '{}' but we do not have the serial of "
            "the original message",
            replySerial);
        throw InternalError{"Internal error: Receiving reply to a message, but the serial is unknown to us"};
      }

      co_await state->replyChannels[replySerial]->async_send(
          boost::system::error_code{}, std::move(message),
          boost::asio::bind_executor(*m_state->strand, boost::asio::use_awaitable));
    }
    // Simply an incoming message
    else
    {
      LOGGER.LogTrace("Received incoming message '{}'", message.GetInfo());

      if (message.GetHeader().GetMessageType() == DBusMessageType::SIGNAL)
      {
        LOGGER.LogTrace("Incoming message is signal, checking match rules");

        for (MatchRuleInfo const& info : *state->matchRules | std::views::values)
        {
          if (info.rule.Matches(message,
                                state->nameCache->GetWellKnownNames(message.GetHeader().GetSender().value_or(""))))
          {
            LOGGER.LogTrace("Rule '{}' matched incoming signal", info.rule.GetRule());
            if (info.callback != nullptr)
            {
              co_await (*info.callback)(message);
            }
            if (info.syncCallback != nullptr)
            {
              (*info.syncCallback)(message);
            }
          }
        }

        co_return;
      }

      if (message.GetHeader().GetObjectPath().has_value() &&
          state->objectPathHandlers->contains(message.GetHeader()
                                                  .GetObjectPath()
                                                  .transform([](ObjectPath const& path) { return path.GetPath(); })
                                                  .value()))
      {
        LOGGER.LogTrace("Message's ObjectPath matches a handler");

        auto handler = (*state->objectPathHandlers)[message.GetHeader().GetObjectPath().value().GetPath()];
        if (!state->messageFilter.empty())
        {
          for (auto const& filter : state->messageFilter | std::views::values)
          {
            if (co_await filter(message) == MessageHandled::YES)
            {
              co_return;
            }
          }
        }

        LOGGER.LogTrace("Invoking ObjectPath handler");
        co_return co_await (*handler)(std::move(message));
      }

      if (!state->onIncomingSignal.empty())
      {
        co_return co_await state->onIncomingSignal(message);
      }

      // If nothing handles our message then we return an error to the sender
      co_await SendMessageNoReply(DBusMessage::Error(message, "org.freedesktop.DBus.Error.UnknownMethod",
                                                     "The method called is not implemented by this connection"));
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
        LOGGER.LogError("Unhandled message loop lost connection to dbus-daemon: {}", ex.what());
        boost::asio::co_spawn(*m_state->ioContext, HandleConnectionLost(), boost::asio::detached);
        break;
      }
      catch (std::exception const& ex)
      {
        LOGGER.LogError("Error occured in unhandled message loop: {}", ex.what());
      }
    }
  }

  boost::asio::awaitable<std::optional<IncomingDBusMessage>> DBusConnection::SendMessageInternal(
      DBusMessage message, boost::asio::io_context& ioContext)
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
                                          boost::asio::bind_executor(*m_state->strand, boost::asio::use_awaitable));

    // 3rd, wait for the message to be sent.
    co_await messageSentChannel->async_receive(
        boost::asio::bind_executor(*m_state->strand, boost::asio::use_awaitable));

    // 4th, check if we're expecting a reply
    if (!expectsReply)
    {
      co_return co_await boost::asio::co_spawn(
          ioContext, []() -> boost::asio::awaitable<std::optional<IncomingDBusMessage>> { co_return std::nullopt; },
          boost::asio::use_awaitable);
    }

    // 5th, wait for the reply to be sent back to us from the ReadLoop() coroutine
    IncomingDBusMessage reply =
        co_await replyChannel.async_receive(boost::asio::bind_executor(*m_state->strand, boost::asio::use_awaitable));
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

    co_return co_await boost::asio::co_spawn(
        ioContext, [reply = std::move(reply)]() -> boost::asio::awaitable<std::optional<IncomingDBusMessage>>
        { co_return reply; }, boost::asio::use_awaitable);
  }

  boost::asio::awaitable<IncomingDBusMessage> DBusConnection::SendMessage(DBusMessage message,
                                                                          boost::asio::io_context& ioContext)
  {
    // Wait until our Connnection is ready
    if (!m_state->connectionReady.load())
    {
      LOGGER.LogTrace("Connection not ready yet, waiting for it to complete");
      m_state->nrOfWaiters++;
      co_await m_state->connectionCompleted.async_receive(
          boost::asio::bind_executor(*m_state->strand, boost::asio::use_awaitable));
    }

    std::optional<IncomingDBusMessage> reply = co_await SendMessageInternal(std::move(message), ioContext);
    if (!reply.has_value())
    {
      LOGGER.LogFatal("SendMessage() should not be able to return without having received a reply");
      throw InternalError{
          "Internal Error: SendMessage() should not be able to return without having received a "
          "reply"};
    }

    co_return co_await boost::asio::co_spawn(
        ioContext, [reply = std::move(reply)]() -> boost::asio::awaitable<IncomingDBusMessage>
        { co_return reply.value(); }, boost::asio::use_awaitable);
  }

  boost::asio::awaitable<IncomingDBusMessage> DBusConnection::SendMessage(DBusMessage message)
  {
    co_return co_await SendMessage(std::move(message), m_userIOContext);
  }

  boost::asio::awaitable<void> DBusConnection::SendMessageNoReply(DBusMessage message,
                                                                  boost::asio::io_context& ioContext)
  {
    // Wait until our Connnection is ready
    if (!m_state->connectionReady.load())
    {
      LOGGER.LogTrace("Connection not ready yet, waiting for it to complete");
      m_state->nrOfWaiters++;
      co_await m_state->connectionCompleted.async_receive(
          boost::asio::bind_executor(*m_state->strand, boost::asio::use_awaitable));
    }

    // Let's auto add the NO_REPLY_EXPECTED flag if it's not been added
    if (!std::ranges::contains(message.GetFlags(), DBusMessageFlags::NO_REPLY_EXPECTED))
    {
      message.Flag(DBusMessageFlags::NO_REPLY_EXPECTED);
    }

    std::ignore = co_await SendMessageInternal(std::move(message), ioContext);

    co_return co_await HopToIOContext(ioContext);
  }

  boost::asio::awaitable<void> DBusConnection::SendMessageNoReply(DBusMessage message)
  {
    co_return co_await SendMessageNoReply(std::move(message), m_userIOContext);
  }

  IncomingDBusMessage DBusConnection::SendMessageSync(DBusMessage message)
  {
    std::optional<IncomingDBusMessage> reply = WaitOnAsyncWork<std::optional<IncomingDBusMessage>>(
        m_state->strand,
        [this, msg = std::move(message)]() { return SendMessageInternal(std::move(msg), *m_state->ioContext); });
    if (!reply.has_value())
    {
      LOGGER.LogFatal("SendMessageSync() should not be able to return without having received a reply");
      throw InternalError{
          "Internal Error: SendMessageSync() should not be able to return without having received a "
          "reply"};
    }

    return reply.value();
  }

  void DBusConnection::SendMessageNoReplySync(DBusMessage message)
  {
    // Let's auto add the NO_REPLY_EXPECTED flag if it's not been added
    if (!std::ranges::contains(message.GetFlags(), DBusMessageFlags::NO_REPLY_EXPECTED))
    {
      message.Flag(DBusMessageFlags::NO_REPLY_EXPECTED);
    }

    std::ignore = WaitOnAsyncWork<std::optional<IncomingDBusMessage>>(
        m_state->strand,
        [this, msg = std::move(message)]() { return SendMessageInternal(std::move(msg), *m_state->ioContext); });
  }

  boost::asio::awaitable<void> DBusConnection::AddMatchRule(
      DBusMatchRule rule, std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback,
      boost::asio::io_context& ioContext)
  {
    LOGGER.LogTrace("Adding match rule '{}'", rule.GetRule());

    co_await SendMessage(DBusMessage::Method("AddMatch")
                             .Path(ObjectPath{"/org/freedesktop/DBus"})
                             .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                             .Destination("org.freedesktop.DBus")
                             .Parameter(rule.GetRule()),
                         ioContext);

    auto const it = std::ranges::find_if(*m_state->matchRules, [&rule](std::pair<uint32_t, MatchRuleInfo> const& elem)
                                         { return elem.second.rule == rule; });
    if (it != m_state->matchRules->end())
    {
      if (it->second.callback == nullptr)
      {
        std::shared_ptr<AwaitableSignal<void, IncomingDBusMessage>> signal =
            std::make_shared<AwaitableSignal<void, IncomingDBusMessage>>();
        signal->connect(
            [cb = std::move(callback)](IncomingDBusMessage message) -> std::function<boost::asio::awaitable<void>()>
            {
              auto state = std::make_shared<std::pair<decltype(cb), IncomingDBusMessage>>(cb, std::move(message));
              return [state]() -> boost::asio::awaitable<void>
              // This cannot be a lambda because the lambda would get destroyed, causing `cb` and `message` to go
              // out-of-scope
              { return InvokeAsyncCallback(state->first, state->second); };
            });

        it->second.callback = std::move(signal);
      }
      else
      {
        it->second.callback->connect(
            [cb = std::move(callback)](IncomingDBusMessage message) -> std::function<boost::asio::awaitable<void>()>
            {
              auto state = std::make_shared<std::pair<decltype(cb), IncomingDBusMessage>>(cb, std::move(message));
              return [state]() -> boost::asio::awaitable<void>
              // This cannot be a lambda because the lambda would get destroyed, causing `cb` and `message` to go
              // out-of-scope
              { return InvokeAsyncCallback(state->first, state->second); };
            });
      }
    }
    else
    {
      std::shared_ptr<AwaitableSignal<void, IncomingDBusMessage>> signal =
          std::make_shared<AwaitableSignal<void, IncomingDBusMessage>>();
      signal->connect(
          [cb = std::move(callback)](IncomingDBusMessage message) -> std::function<boost::asio::awaitable<void>()>
          {
            auto state = std::make_shared<std::pair<decltype(cb), IncomingDBusMessage>>(cb, std::move(message));
            return [state]() -> boost::asio::awaitable<void>
            // This cannot be a lambda because the lambda would get destroyed, causing `cb` and `message` to go
            // out-of-scope
            { return InvokeAsyncCallback(state->first, state->second); };
          });

      m_state->matchRules->emplace(
          (*m_state->subscriptionCounter)++,
          MatchRuleInfo{.rule = std::move(rule), .callback = std::move(signal), .syncCallback = nullptr});
    }

    co_return co_await HopToIOContext(ioContext);
  }

  boost::asio::awaitable<void> DBusConnection::AddMatchRule(
      DBusMatchRule rule, std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback)
  {
    co_return co_await AddMatchRule(std::move(rule), std::move(callback), m_userIOContext);
  }

  boost::asio::awaitable<void> DBusConnection::RemoveMatchRule(DBusMatchRule rule, boost::asio::io_context& ioContext)
  {
    LOGGER.LogTrace("Removing match rule '{}'", rule.GetRule());

    co_await SendMessage(DBusMessage::Method("RemoveMatch")
                             .Path(ObjectPath{"/org/freedesktop/DBus"})
                             .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                             .Destination("org.freedesktop.DBus")
                             .Parameter(rule.GetRule()),
                         ioContext);

    auto const it = std::ranges::find_if(*m_state->matchRules, [&rule](std::pair<uint32_t, MatchRuleInfo> const& elem)
                                         { return elem.second.rule == rule; });
    m_state->matchRules->erase(it);

    co_return co_await HopToIOContext(ioContext);
  }

  boost::asio::awaitable<void> DBusConnection::RemoveMatchRule(DBusMatchRule rule)
  {
    co_return co_await RemoveMatchRule(std::move(rule), m_userIOContext);
  }

  void DBusConnection::AddMatchRuleSync(DBusMatchRule rule, std::function<void(IncomingDBusMessage)> callback)
  {
    LOGGER.LogTrace("Adding match rule '{}'", rule.GetRule());

    WaitOnAsyncWork<IncomingDBusMessage>(m_state->strand,
                                         [this, rule]()
                                         {
                                           return SendMessage(DBusMessage::Method("AddMatch")
                                                                  .Path(ObjectPath{"/org/freedesktop/DBus"})
                                                                  .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                                                  .Destination("org.freedesktop.DBus")
                                                                  .Parameter(rule.GetRule()),
                                                              *m_state->ioContext);
                                         });

    auto const it = std::ranges::find_if(*m_state->matchRules, [&rule](std::pair<uint32_t, MatchRuleInfo> const& elem)
                                         { return elem.second.rule == rule; });
    if (it != m_state->matchRules->end())
    {
      if (it->second.syncCallback == nullptr)
      {
        std::shared_ptr<boost::signals2::signal<void(IncomingDBusMessage)>> signal =
            std::make_shared<boost::signals2::signal<void(IncomingDBusMessage)>>();
        signal->connect(std::move(callback));

        it->second.syncCallback = std::move(signal);
      }
      else
      {
        it->second.syncCallback->connect(std::move(callback));
      }
    }
    else
    {
      std::shared_ptr<boost::signals2::signal<void(IncomingDBusMessage)>> signal =
          std::make_shared<boost::signals2::signal<void(IncomingDBusMessage)>>();
      signal->connect(std::move(callback));

      m_state->matchRules->emplace(
          (*m_state->subscriptionCounter)++,
          MatchRuleInfo{.rule = std::move(rule), .callback = nullptr, .syncCallback = std::move(signal)});
    }
  }

  void DBusConnection::RemoveMatchRuleSync(DBusMatchRule rule)
  {
    LOGGER.LogTrace("Removing match rule '{}'", rule.GetRule());

    WaitOnAsyncWork<IncomingDBusMessage>(m_state->strand,
                                         [this, rule]()
                                         {
                                           return SendMessage(DBusMessage::Method("RemoveMatch")
                                                                  .Path(ObjectPath{"/org/freedesktop/DBus"})
                                                                  .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                                                  .Destination("org.freedesktop.DBus")
                                                                  .Parameter(rule.GetRule()),
                                                              *m_state->ioContext);
                                         });

    auto const it = std::ranges::find_if(*m_state->matchRules, [&rule](std::pair<uint32_t, MatchRuleInfo> const& elem)
                                         { return elem.second.rule == rule; });
    m_state->matchRules->erase(it);
  }

  void DBusConnection::RegisterObjectPathHandler(
      ObjectPath path, std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback)
  {
    auto cb = [cb = std::move(callback)](IncomingDBusMessage message) -> std::function<boost::asio::awaitable<void>()>
    {
      auto state = std::make_shared<std::pair<decltype(cb), IncomingDBusMessage>>(cb, std::move(message));
      return [state]() -> boost::asio::awaitable<void>
      // This cannot be a lambda because the lambda would get destroyed, causing `cb` and `message` to go
      // out-of-scope
      { return InvokeAsyncCallback(state->first, state->second); };
    };
    if (auto it = m_state->objectPathHandlers->find(path.GetPath()); it != m_state->objectPathHandlers->end())
    {
      it->second->connect(std::move(cb));
    }
    else
    {
      std::shared_ptr<AwaitableSignal<void, IncomingDBusMessage>> signal =
          std::make_shared<AwaitableSignal<void, IncomingDBusMessage>>();
      signal->connect(std::move(cb));
      (*m_state->objectPathHandlers)[path.GetPath()] = signal;
    }
  }

  void DBusConnection::UnregisterObjectPathHandler(ObjectPath path)
  {
    (*m_state->objectPathHandlers).erase(path.GetPath());
  }

  boost::asio::awaitable<void> DBusConnection::RequestWellKnownName(DBusWellKnownName name,
                                                                    boost::asio::io_context& ioContext)
  {
    if (std::ranges::contains(*m_state->wellKnownNames, name))
    {
      throw std::runtime_error{std::format("This connection already owns the name '{}'", name.GetName())};

      auto reply = co_await SendMessage(
          DBusMessage::Method("RequestName")
              .Path(ObjectPath{"/org/freedesktop/DBus"})
              .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
              .Destination("org.freedesktop.DBus")
              .Parameter(MultipleCompleteTypes<std::string, uint32_t>{name.GetName(), static_cast<uint32_t>(0x1)}),
          ioContext);

      switch (reply.Get<uint32_t>())
      {
        case 1:
          LOGGER.LogDebug("Successfully acquired well-known name '{}'", name.GetName());
          break;
        // [TODO]: Allow user passing flags for the Well-known name.
        case 2:
          LOGGER.LogError(
              "Well-known name '{}' is already owned by another connection and we did "
              "not ask to replace the name",
              name.GetName());
          break;
        case 3:
          LOGGER.LogError("The well-known name '{}' already has an owner", name.GetName());
          break;
        case 4:
          LOGGER.LogDebug("We're already owner of our well-known name");
          break;
        default:
          LOGGER.LogError("Unknown return value from 'RequestName()': {}", reply.Get<uint32_t>());
          break;
      }

      m_state->wellKnownNames->push_back(name);
    }

    co_return co_await HopToIOContext(ioContext);
  }

  boost::asio::awaitable<void> DBusConnection::RequestWellKnownName(DBusWellKnownName name)
  {
    co_return co_await RequestWellKnownName(std::move(name), m_userIOContext);
  }

  boost::asio::awaitable<void> DBusConnection::ReleaseWellKnownName(DBusWellKnownName name,
                                                                    boost::asio::io_context& ioContext)
  {
    if (!std::ranges::contains(*m_state->wellKnownNames, name))
    {
      throw std::runtime_error{std::format("This connection does not own the name '{}'", name.GetName())};
    }

    LOGGER.LogTrace("Releasing our well-known name '{}'", name.GetName());
    IncomingDBusMessage const ret = co_await SendMessage(DBusMessage::Method("ReleaseName")
                                                             .Path(ObjectPath{"/org/freedesktop/DBus"})
                                                             .Destination("org.freedesktop.DBus")
                                                             .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                                             .Parameter(name.GetName()),
                                                         ioContext);

    uint32_t const res = ret.Get<uint32_t>();
    switch (res)
    {
      case 1:
        LOGGER.LogDebug("Successfully released well-known name '{}'", name.GetName());
        break;
      case 2:
        LOGGER.LogError("Well-known name '{}' is not owned by the dbus-daemon", name.GetName());
        break;
      case 3:
        LOGGER.LogError("Well-known name '{}' is not owned by this connection", name.GetName());
        break;
      default:
        LOGGER.LogError("Unknown return value from 'ReleaseName()': {}", res);
        break;
    }

    auto it = std::ranges::remove(*m_state->wellKnownNames, name);
    m_state->wellKnownNames->erase(it.begin(), it.end());

    co_return co_await HopToIOContext(ioContext);
  }

  boost::asio::awaitable<void> DBusConnection::ReleaseWellKnownName(DBusWellKnownName name)
  {
    co_return co_await ReleaseWellKnownName(std::move(name), m_userIOContext);
  }

  void DBusConnection::RequestWellKnownNameSync(DBusWellKnownName name)
  {
    WaitOnAsyncWork<void>(m_state->strand, [this, name = std::move(name)]
                          { return RequestWellKnownName(std::move(name), *m_state->ioContext); });
  }

  void DBusConnection::ReleaseWellKnownNameSync(DBusWellKnownName name)
  {
    WaitOnAsyncWork<void>(m_state->strand, [this, name = std::move(name)]
                          { return ReleaseWellKnownName(std::move(name), *m_state->ioContext); });
  }

  uint32_t DBusConnection::RegisterMessageFilter(
      std::function<boost::asio::awaitable<MessageHandled>(IncomingDBusMessage)> callback)
  {
    uint32_t filterID = m_state->messageFilterID++;
    m_state->messageFilter[filterID].connect(
        [cb = std::move(callback)](
            IncomingDBusMessage message) -> std::function<boost::asio::awaitable<MessageHandled>()>
        {
          auto state = std::make_shared<std::pair<decltype(cb), IncomingDBusMessage>>(cb, std::move(message));
          return [state]() -> boost::asio::awaitable<MessageHandled>
          // This cannot be a lambda because the lambda would get destroyed, causing `cb` and `message` to go
          // out-of-scope
          { return InvokeAsyncCallback(state->first, state->second); };
        });
    return filterID;
  }

  void DBusConnection::UnregisterMessageFilter(uint32_t filterID)
  {
    m_state->messageFilter.erase(filterID);
  }

  void DBusConnection::ReceiveIncomingMessages(
      std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback)
  {
    m_state->onIncomingSignal.connect(
        [cb = std::move(callback)](IncomingDBusMessage message) -> std::function<boost::asio::awaitable<void>()>
        {
          auto state = std::make_shared<std::pair<decltype(cb), IncomingDBusMessage>>(cb, std::move(message));
          return [state]() -> boost::asio::awaitable<void>
          // This cannot be a lambda because the lambda would get destroyed, causing `cb` and `message` to go
          // out-of-scope
          { return InvokeAsyncCallback(state->first, state->second); };
        });
  }

  std::vector<DBusWellKnownName> const& DBusConnection::GetWellKnownNames() const
  {
    return *m_state->wellKnownNames;
  }

  bool DBusConnection::IsConnected() const
  {
    return m_state->connectionReady.load();
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
      boost::asio::co_spawn(
          *m_state->ioContext,
          [this]() -> boost::asio::awaitable<void>
          {
            // Hard shutdown the socket, this should cause the HandleConnectionLoss() function to get called
            boost::system::error_code ec;
            std::ignore = m_state->socket->shutdown(boost::asio::local::stream_protocol::socket::shutdown_both, ec);
            co_return;
          },
          boost::asio::detached);
    }
  }
}  // namespace cxxbus
