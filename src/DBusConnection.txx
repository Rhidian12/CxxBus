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

#pragma once

#include <algorithm>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/signals2/connection.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <cstdint>
#include <exception>
#include <format>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>

#include "DBusConnection.h"
#include "DBusHelpers.h"
#include "DBusMatchRule.h"
#include "DBusMessage.h"
#include "DBusNameCache.h"
#include "DBusTypes.h"
#include "IncomingDBusMessage.h"
#include "Log.h"

namespace cxxbus
{
  using namespace std::chrono_literals;

#ifndef CXX_BUS_EXIT_IF_EXPIRED
#define CXX_BUS_EXIT_IF_EXPIRED(var) \
  if ((var).expired())               \
  {                                  \
    co_return;                       \
  }
#endif  // CXX_BUS_EXIT_IF_EXPIRED

  inline void IOThread(std::shared_ptr<boost::asio::io_context> ioContext)
  {
    ioContext->run();
  }

  template <typename T>
  T WaitOnAsyncWork(boost::asio::strand<typename boost::asio::io_context::executor_type>& strand,
                    std::function<boost::asio::awaitable<T>()> work)
  {
    std::promise<T> promise;
    std::future<T> future = promise.get_future();

    if constexpr (!std::is_void_v<T>)
    {
      boost::asio::co_spawn(strand, work(),
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
      boost::asio::co_spawn(strand, work(),
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

  template <bool SingleThreaded>
  boost::asio::awaitable<std::shared_ptr<DBusConnectionImpl<SingleThreaded>>>
  DBusConnectionImpl<SingleThreaded>::Create(boost::asio::io_context& userIOContext,
                                             std::optional<DBusWellKnownName> wellKnownName, BusType busType)
  {
    std::shared_ptr<DBusConnectionImpl> conn{new DBusConnectionImpl(userIOContext, std::move(wellKnownName))};

    if constexpr (SingleThreaded)
    {
      co_await conn->Connect(busType);
    }
    else
    {
      co_await boost::asio::co_spawn(conn->m_state->activeContext, conn->Connect(busType), boost::asio::use_awaitable);
    }

    co_return conn;
  }

  template <bool SingleThreaded>
  std::shared_ptr<DBusConnectionImpl<SingleThreaded>> DBusConnectionImpl<SingleThreaded>::CreateDetached(
      boost::asio::io_context& userIOContext, std::optional<DBusWellKnownName> wellKnownName,
      std::function<boost::asio::awaitable<void>()> onConnectedCallback, BusType busType)
  {
    std::shared_ptr<DBusConnectionImpl> conn{new DBusConnectionImpl(userIOContext, std::move(wellKnownName))};

    boost::asio::co_spawn(conn->m_state->activeContext, conn->Connect(busType),
                          [&userIOContext, cb = std::move(onConnectedCallback)](std::exception_ptr e)
                          {
                            if (e)
                            {
                              std::rethrow_exception(e);
                            }

                            if (cb)
                            {
                              boost::asio::co_spawn(userIOContext, std::move(cb), boost::asio::detached);
                            }
                          });

    return conn;
  }

  template <bool SingleThreaded>
  std::shared_ptr<DBusConnectionImpl<SingleThreaded>> DBusConnectionImpl<SingleThreaded>::CreateSync(
      boost::asio::io_context& ioService, std::optional<DBusWellKnownName> wellKnownName, BusType busType)
  {
    std::shared_ptr<DBusConnectionImpl> conn{new DBusConnectionImpl(ioService, std::move(wellKnownName))};
    WaitOnAsyncWork<void>(*conn->m_state->strand,
                          [conn, busType]() -> boost::asio::awaitable<void> { return conn->Connect(busType); });

    return conn;
  }

  template <bool SingleThreaded>
  DBusConnectionImpl<SingleThreaded>::DBusConnectionImpl(boost::asio::io_context& ioService,
                                                         std::optional<DBusWellKnownName> wellKnownName)
    : m_state()
    , m_userIOContext(ioService)
  {
    std::shared_ptr<boost::asio::io_context> ioContext{std::make_shared<boost::asio::io_context>()};
    boost::asio::any_io_executor activeContext{m_userIOContext.get_executor()};

    std::unique_ptr<boost::asio::strand<typename boost::asio::io_context::executor_type>> strand;
    if constexpr (!SingleThreaded)
    {
      strand = std::make_unique<boost::asio::strand<typename boost::asio::io_context::executor_type>>(
          ioContext->get_executor());
      activeContext = *strand;
    }

    m_state = std::shared_ptr<InternalState>(new InternalState{
        .ioContext = ioContext,
        .replyChannels = {},
        .onIncomingSignal = {},
        .messageFilters = {},
        .messageFilterID = 0,
        .onDisconnected = {},
        .connectionReady = false,
        .connectionCompleted = boost::asio::experimental::channel<void(boost::system::error_code)>{activeContext},
        .nrOfWaiters = 0,
        .strand = std::move(strand),
        .socket = boost::asio::local::stream_protocol::socket(activeContext),
        .uniqueConnection = std::nullopt,
        .wellKnownNames = {},
        .serial = 1,
        .matchRules = {},
        .nameCache = std::make_shared<DBusNameCache<SingleThreaded>>(*this),
        .objectPathHandlers = {},
        .mutex = {},
        .workGuard = nullptr,
        .ioThread = nullptr,
        .activeContext = activeContext,
        .shouldQuit = false,
        .readLoopFinished = boost::asio::experimental::channel<void(boost::system::error_code)>{activeContext}});

    if (wellKnownName.has_value())
    {
      m_state->wellKnownNames.push_back(*wellKnownName);
    }

    if constexpr (!SingleThreaded)
    {
      m_state->workGuard =
          std::make_unique<boost::asio::executor_work_guard<typename boost::asio::io_context::executor_type>>(
              boost::asio::make_work_guard(*ioContext));
      m_state->ioThread = std::make_shared<std::thread>(&IOThread, ioContext);
    }

    for (int i{}; i < CXX_BUS_MAX_CONCURRENT_MESSAGES; ++i)
    {
      m_state->replyChannels.emplace_back(
          boost::asio::experimental::channel<void(boost::system::error_code, IncomingDBusMessage)>{
              m_state->activeContext, 1},
          true);
    }
  }

  template <bool SingleThreaded>
  DBusConnectionImpl<SingleThreaded>::~DBusConnectionImpl()
  {
    if constexpr (SingleThreaded)
    {
      boost::asio::co_spawn(m_state->activeContext, Close(), boost::asio::detached);
    }
    else
    {
      CloseSync();
    }
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::CloseData()
  {
    LOG_TRACE(LOGGER, "Closing channels and signals");
    m_state->onIncomingSignal.clear();
    m_state->objectPathHandlers.clear();
    m_state->shouldQuit = true;
    m_state->connectionReady = false;

    LOG_TRACE(LOGGER, "Closing socket");
    if (m_state->socket.is_open())
    {
      boost::system::error_code ec;
      std::ignore = m_state->socket.close(ec);
    }
    LOG_TRACE(LOGGER, "Closed socket");

    co_await m_state->readLoopFinished.async_receive(boost::asio::use_awaitable);
    LOG_TRACE(LOGGER, "Read loop has fully finished");

    co_return;
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::HandleConnectionLost()
  {
    if (m_state->shouldQuit)
    {
      co_return;
    }

    LOG_ERROR(LOGGER, "Connection to the dbus-daemon was lost unexpectedly");

    m_state->connectionReady = false;
    co_await Close(DONT_HOP);
    boost::asio::co_spawn(
        m_userIOContext,
        [this]() -> boost::asio::awaitable<void>
        {
          m_state->onDisconnected();
          co_return;
        },
        boost::asio::detached);
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::CloseImpl()
  {
    if (!m_state->socket.is_open())
    {
      // Already closed
      co_return;
    }

    LOG_TRACE(LOGGER, "Closing DBus Connection");

    if (m_state->connectionReady)
    {
      std::vector<MatchRuleInfo> rules{m_state->matchRules};
      for (MatchRuleInfo const& ruleInfo : rules)
      {
        co_await RemoveMatchRule(ruleInfo.rule, DONT_HOP);
      }

      std::vector<DBusWellKnownName> names{m_state->wellKnownNames};
      // Release our well-known name from the dbus-daemon
      LOG_TRACE(LOGGER, "Releasing our well-known name");
      for (DBusWellKnownName name : names)
      {
        co_await ReleaseWellKnownName(name, DONT_HOP);
      }
    }

    co_await CloseData();
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::Close()
  {
    if constexpr (SingleThreaded)
    {
      co_return co_await CloseImpl();
    }
    else
    {
      co_return co_await boost::asio::co_spawn(m_state->activeContext, CloseImpl(), boost::asio::use_awaitable);
    }
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::Close(DontHopTag)
  {
    co_return co_await CloseImpl();
  }

  template <bool SingleThreaded>
  void DBusConnectionImpl<SingleThreaded>::CloseSync()
  {
    if (!m_state->strand)
    {
      return;
    }

    if (m_state->strand->running_in_this_thread())
    {
      throw std::logic_error("CloseSync() cannot be called from the DBus IO thread");
    }

    if (!m_state->ioThread->joinable())  // thread already closed
    {
      return;
    }
    LOG_TRACE(LOGGER, "Synchronously closing the connection");
    WaitOnAsyncWork<void>(*m_state->strand, [this]() { return CloseImpl(); });

    LOG_TRACE(LOGGER, "Joining thread");
    m_state->workGuard.reset();
    m_state->ioThread->join();
    LOG_TRACE(LOGGER, "Joined thread");
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::AuthenticateDBusConnectionImpl()
  {
    std::weak_ptr<DBusConnectionImpl> weakThis{this->shared_from_this()};
    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    std::shared_ptr<InternalState> state = m_state;

    // First send a single '\0' byte
    co_await state->socket.async_send(boost::asio::buffer("\0", 1), boost::asio::use_awaitable);
    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    // Next we must authenticate ourselves, we use the EXTERNAL
    // authentication method
    std::string const auth = HexEncodeString(std::to_string(::getuid()));
    co_await state->socket.async_send(boost::asio::buffer(std::format("AUTH EXTERNAL {}\r\n", auth)),
                                      boost::asio::use_awaitable);
    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    // Now we expect to see OK <guid>
    std::string reply{};
    co_await boost::asio::async_read_until(state->socket, boost::asio::dynamic_buffer(reply), "\r\n",
                                           boost::asio::use_awaitable);
    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    if (!reply.starts_with("OK"))
    {
      LOG_ERROR(LOGGER, "Authentication failed!");
      throw std::runtime_error{"Authentication failed!"};
    }

    // Yippee! All worked, so now start our DBus Connection!
    co_await state->socket.async_send(boost::asio::buffer("BEGIN\r\n", 7), boost::asio::use_awaitable);
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::Connect(BusType busType)
  {
    LOG_TRACE(LOGGER, "Starting connection coroutine");
    std::weak_ptr<DBusConnectionImpl> weakThis{this->shared_from_this()};
    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    std::shared_ptr<InternalState> state = m_state;

    // Connect to DBus daemon
    if (busType == BusType::SESSION)
    {
      boost::asio::local::stream_protocol::endpoint endpoint{ParseDBusAddress(busType)};
      LOG_TRACE(LOGGER, "CONNECTING to {}", ParseDBusAddress(busType));
      co_await state->socket.async_connect(endpoint, boost::asio::as_tuple(boost::asio::use_awaitable));
      LOG_TRACE(LOGGER, "Connected to DBus Session bus");
    }
    else
    {
      std::string address = ParseDBusAddress(busType);
      if (address.empty())
      {
        address = "/var/run/dbus/system_bus_socket";
      }
      boost::asio::local::stream_protocol::endpoint endpoint{address};
      co_await state->socket.async_connect(endpoint, boost::asio::as_tuple(boost::asio::use_awaitable));
      LOG_TRACE(LOGGER, "Connected to DBus System bus");
    }

    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    co_await AuthenticateDBusConnectionImpl();

    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    LOG_TRACE(LOGGER, "Connected to DBus-daemon. Starting Read loop");
    boost::asio::co_spawn(m_state->activeContext, ReadLoop(), boost::asio::detached);

    LOG_TRACE(LOGGER, "Read loop started. Starting connection handshake");

    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    // Get our unique bus name
    std::optional<IncomingDBusMessage> reply =
        co_await SendMessageInternal(std::move(DBusMessage::Method("Hello")
                                                   .Path(ObjectPath{"/org/freedesktop/DBus"})
                                                   .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                                   .Destination("org.freedesktop.DBus")));
    if (reply.has_value())
    {
      m_state->uniqueConnection = DBusUniqueConnectionName(reply->Get<std::string>());
    }

    LOG_INFO(LOGGER, "Unique Connection ID: {}", m_state->uniqueConnection->GetName());

    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    // Now, request a well-known name from the dbus-daemon
    for (DBusWellKnownName name : m_state->wellKnownNames)
    {
      reply = co_await SendMessageInternal(std::move(
          DBusMessage::Method("RequestName")
              .Path(ObjectPath{"/org/freedesktop/DBus"})
              .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
              .Destination("org.freedesktop.DBus")
              .Parameter(MultipleCompleteTypes<std::string, uint32_t>{name.GetName(), static_cast<uint32_t>(0x1)})));

      if (!reply.has_value())
      {
        LOG_FATAL(LOGGER,
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
          LOG_DEBUG(LOGGER, "Successfully acquired well-known name '{}'", name.GetName());
          break;
        // [TODO]: Allow user passing flags for the Well-known name.
        case 2:
          LOG_ERROR(LOGGER,
                    "Well-known name '{}' is already owned by another connection and we did "
                    "not ask to replace the name",
                    name.GetName());
          break;
        case 3:
          LOG_ERROR(LOGGER, "The well-known name '{}' already has an owner", name.GetName());
          break;
        case 4:
          LOG_DEBUG(LOGGER, "We're already owner of our well-known name");
          break;
        default:
          LOG_ERROR(LOGGER, "Unknown return value from 'RequestName()': {}", ret);
          break;
      }
    }

    LOG_TRACE(LOGGER, "Connection handshake completed.");

    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    m_state->connectionReady = true;
    if (m_state->nrOfWaiters > 0)
    {
      co_await m_state->connectionCompleted.async_send(boost::system::error_code{}, boost::asio::use_awaitable);
    }

    CXX_BUS_EXIT_IF_EXPIRED(weakThis)

    LOG_TRACE(LOGGER, "Subscribing to NameOwnerChanged signal");
    co_await m_state->nameCache->SubscribeToNameChanges();

    co_return;
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::HandleReadMessage(IncomingDBusMessage&& message)
  {
    std::shared_ptr<InternalState> state = m_state;
    DBusMessageHeader const& messageHeader = message.GetHeader();

    // We're dealing with a reply from a previously sent message
    if (messageHeader.GetReplySerial().has_value())
    {
      uint32_t const replySerial{messageHeader.GetReplySerial().value()};
      LOG_TRACE(LOGGER, "Received reply to message with serial '{}'. Reply: '{}'", replySerial, message.GetInfo());

      // This can only be set to 'true' if we didn't send a message with this serial first
      // which should be impossible
      if (state->replyChannels[replySerial % CXX_BUS_MAX_CONCURRENT_MESSAGES].ready)
      {
        // It should not be possible to get a reply to a message we don't know
        LOG_FATAL(LOGGER,
                  "Received a reply with serial '{}' but we do not have the serial of "
                  "the original message",
                  replySerial);
        throw InternalError{"Internal error: Receiving reply to a message, but the serial is unknown to us"};
      }

      co_await state->replyChannels[replySerial % CXX_BUS_MAX_CONCURRENT_MESSAGES].channel.async_send(
          boost::system::error_code{}, std::move(message), boost::asio::use_awaitable);
    }
    // Simply an incoming message
    else
    {
      LOG_TRACE(LOGGER, "Received incoming message '{}'", message.GetInfo());

      if (messageHeader.GetMessageType() == DBusMessageType::SIGNAL)
      {
        LOG_TRACE(LOGGER, "Incoming message is signal, checking match rules");

        for (MatchRuleInfo const& info : state->matchRules)
        {
          if (info.rule.Matches(message,
                                state->nameCache->GetWellKnownNames(message.GetHeader().GetSender().value_or(""))))
          {
            LOG_TRACE(LOGGER, "Rule '{}' matched incoming signal", info.rule.GetRule());
            boost::asio::io_context& ioContext{info.executeOnUserContext ? m_userIOContext : *m_state->ioContext};
            if (!info.callback.empty())
            {
              boost::asio::co_spawn(
                  ioContext,
                  [info, message = message]() -> boost::asio::awaitable<void>
                  {
                    for (std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> const& cb :
                         info.callback)
                    {
                      co_await cb(message);
                    }
                    co_return;
                  },
                  boost::asio::detached);
            }
          }
        }

        co_return;
      }

      std::string const path =
          messageHeader.GetObjectPath().transform([](ObjectPath const& path) { return path.GetPath(); }).value_or("");

      if (state->objectPathHandlers.contains(path))
      {
        LOG_TRACE(LOGGER, "Message's ObjectPath matches a handler");

        for (auto const& [_, filter] : state->messageFilters)
        {
          if (co_await filter(message) == MessageHandled::YES)
          {
            co_return;
          }
        }

        LOG_TRACE(LOGGER, "Invoking ObjectPath handler");
        boost::asio::co_spawn(
            m_userIOContext,
            [message = std::move(message), state = std::move(state)]() mutable -> boost::asio::awaitable<void>
            {
              for (auto const& [_, handlers] : state->objectPathHandlers)
              {
                for (std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> const& handler : handlers)
                {
                  co_await handler(message);
                }
              }
            },
            boost::asio::detached);
        co_return;
      }

      // [TODO]: User should let us know whether they actually handled this or not
      if (!state->onIncomingSignal.empty())
      {
        LOG_TRACE(LOGGER, "OnIncoming has subscribers, so calling those");
        boost::asio::co_spawn(
            m_userIOContext,
            [state = std::move(state), message = std::move(message)]() -> boost::asio::awaitable<void>
            {
              for (std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> const& signal :
                   state->onIncomingSignal)
              {
                co_await signal(message);
              }
              co_return;
            },
            boost::asio::detached);
        co_return;
      }

      // If nothing handles our message then we return an error to the sender
      co_await SendMessageNoReply(DBusMessage::Error(message, "org.freedesktop.DBus.Error.UnknownMethod",
                                                     "The method called is not implemented by this connection"));
    }
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<std::optional<IncomingDBusMessage>> DBusConnectionImpl<SingleThreaded>::SendMessageInternal(
      DBusMessage&& message)
  {
    // 1st, if we're expecting a reply, store a channel so we can await a reply from the dbus-daemon
    bool const expectsReply{!std::ranges::contains(message.GetFlags(), DBusMessageFlags::NO_REPLY_EXPECTED)};
    uint32_t const serial = m_state->serial++;
    // [TODO]: Logic doesnt fully make sense what if 1 not sent but all other messages are sent and we loop back around
    // to 1?
    ChannelInfo& channInfo{m_state->replyChannels[serial % CXX_BUS_MAX_CONCURRENT_MESSAGES]};

    if (expectsReply)
    {
      channInfo.ready = false;
    }

    // Write our actual message
    co_await boost::asio::async_write(m_state->socket, boost::asio::buffer(message.Serialize(serial)),
                                      boost::asio::use_awaitable);

    LOG_TRACE(LOGGER, "Sent message '{}' with serial '{}'", message.GetInfo(), m_state->serial);

    // 4th, check if we're expecting a reply
    if (!expectsReply)
    {
      co_return std::nullopt;
    }

    // 5th, wait for the reply to be sent back to us from the ReadLoop() coroutine
    IncomingDBusMessage reply = co_await channInfo.channel.async_receive(boost::asio::use_awaitable);
    channInfo.ready = true;

    if (reply.GetHeader().GetMessageType() == DBusMessageType::ERROR) [[unlikely]]
    {
      // We got an error, so throw an error here
      if (!reply.GetHeader().GetErrorName().has_value())
      {
        LOG_TRACE(LOGGER, "Incoming DBus Error did not specify the ERROR_NAME header field");
      }

      throw DBusError{
          reply.GetHeader().GetErrorName().has_value() ? reply.GetHeader().GetErrorName().value() : "Missing",
          reply.HasArguments() && reply.GetHeader().GetSignature().value_or(Signature{""}) == "s"
              ? reply.Get<std::string>()
              : "No error message was provided by the remote"};
    }

    co_return reply;
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<IncomingDBusMessage> DBusConnectionImpl<SingleThreaded>::SendMessageImpl(DBusMessage message)
  {
    // Wait until our Connnection is ready
    if (!m_state->connectionReady) [[unlikely]]
    {
      LOG_TRACE(LOGGER, "Connection not ready yet, waiting for it to complete");
      m_state->nrOfWaiters++;
      co_await m_state->connectionCompleted.async_receive(boost::asio::use_awaitable);
    }

    std::optional<IncomingDBusMessage> reply = co_await SendMessageInternal(std::move(message));
    if (!reply.has_value()) [[unlikely]]
    {
      LOG_TRACE(LOGGER, "SendMessage() should not be able to return without having received a reply");
      throw InternalError{
          "Internal Error: SendMessage() should not be able to return without having received a "
          "reply"};
    }

    co_return reply.value();
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<IncomingDBusMessage> DBusConnectionImpl<SingleThreaded>::SendMessage(DBusMessage&& message)
  {
    if constexpr (SingleThreaded)
    {
      co_return co_await SendMessageImpl(std::move(message));
    }
    else
    {
      co_return co_await boost::asio::co_spawn(m_state->activeContext, SendMessageImpl(std::move(message)),
                                               boost::asio::use_awaitable);
    }
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<IncomingDBusMessage> DBusConnectionImpl<SingleThreaded>::SendMessage(
      DBusMessage const& message)
  {
    if constexpr (SingleThreaded)
    {
      co_return co_await SendMessageImpl(message);
    }
    else
    {
      co_return co_await boost::asio::co_spawn(m_state->activeContext, SendMessageImpl(message),
                                               boost::asio::use_awaitable);
    }
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<IncomingDBusMessage> DBusConnectionImpl<SingleThreaded>::SendMessage(DBusMessage message,
                                                                                              DontHopTag)
  {
    co_return co_await SendMessageImpl(std::move(message));
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::SendMessageNoReplyImpl(DBusMessage message)
  {
    // Wait until our Connnection is ready
    if (!m_state->connectionReady)
    {
      LOG_TRACE(LOGGER, "Connection not ready yet, waiting for it to complete");
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

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::SendMessageNoReply(DBusMessage message)
  {
    if constexpr (SingleThreaded)
    {
      co_await SendMessageNoReplyImpl(std::move(message));
    }
    else
    {
      co_await boost::asio::co_spawn(m_state->activeContext, SendMessageNoReplyImpl(std::move(message)),
                                     boost::asio::use_awaitable);
    }
  }

  template <bool SingleThreaded>
  IncomingDBusMessage DBusConnectionImpl<SingleThreaded>::SendMessageSync(DBusMessage message)
  {
    std::optional<IncomingDBusMessage> reply = WaitOnAsyncWork<std::optional<IncomingDBusMessage>>(
        *m_state->strand, [this, msg = std::move(message)]() mutable { return SendMessageInternal(std::move(msg)); });

    if (!reply.has_value())
    {
      LOG_FATAL(LOGGER, "SendMessageSync() should not be able to return without having received a reply");
      throw InternalError{
          "Internal Error: SendMessageSync() should not be able to return without having received a "
          "reply"};
    }

    return reply.value();
  }

  template <bool SingleThreaded>
  void DBusConnectionImpl<SingleThreaded>::SendMessageNoReplySync(DBusMessage message)
  {
    // Let's auto add the NO_REPLY_EXPECTED flag if it's not been added
    if (!std::ranges::contains(message.GetFlags(), DBusMessageFlags::NO_REPLY_EXPECTED))
    {
      message.Flag(DBusMessageFlags::NO_REPLY_EXPECTED);
    }

    std::ignore = WaitOnAsyncWork<std::optional<IncomingDBusMessage>>(
        *m_state->strand, [this, msg = std::move(message)]() mutable { return SendMessageInternal(std::move(msg)); });
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::AddMatchRuleImpl(
      DBusMatchRule rule, std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback,
      bool executeOnUserContext)
  {
    LOG_TRACE(LOGGER, "Adding match rule '{}'", rule.GetRule());

    co_await SendMessage(DBusMessage::Method("AddMatch")
                             .Path(ObjectPath{"/org/freedesktop/DBus"})
                             .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                             .Destination("org.freedesktop.DBus")
                             .Parameter(rule.GetRule()),
                         DONT_HOP);

    auto const it =
        std::ranges::find_if(m_state->matchRules, [&rule](MatchRuleInfo const& elem) { return elem.rule == rule; });
    if (it != m_state->matchRules.end())
    {
      it->callback.push_back(std::move(callback));
    }
    else
    {
      m_state->matchRules.emplace_back(std::move(rule), std::vector{std::move(callback)}, executeOnUserContext);
    }

    co_return;
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::AddMatchRule(
      DBusMatchRule rule, std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback,
      bool executeOnUserContext)
  {
    if constexpr (SingleThreaded)
    {
      co_await AddMatchRuleImpl(std::move(rule), std::move(callback), executeOnUserContext);
    }
    else
    {
      co_return co_await boost::asio::co_spawn(
          m_state->activeContext, AddMatchRuleImpl(std::move(rule), std::move(callback), executeOnUserContext));
    }
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::AddMatchRule(
      DBusMatchRule rule, std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback)
  {
    if constexpr (SingleThreaded)
    {
      co_await AddMatchRuleImpl(std::move(rule), std::move(callback));
    }
    else
    {
      co_await boost::asio::co_spawn(m_state->activeContext,
                                     AddMatchRuleImpl(std::move(rule), std::move(callback), true),
                                     boost::asio::use_awaitable);
    }
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::AddMatchRule(
      DBusMatchRule rule, std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback, DontHopTag)
  {
    co_return co_await AddMatchRuleImpl(std::move(rule), std::move(callback), true);
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::RemoveMatchRuleImpl(DBusMatchRule rule)
  {
    LOG_TRACE(LOGGER, "Removing match rule '{}'", rule.GetRule());

    co_await SendMessage(DBusMessage::Method("RemoveMatch")
                             .Path(ObjectPath{"/org/freedesktop/DBus"})
                             .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                             .Destination("org.freedesktop.DBus")
                             .Parameter(rule.GetRule()),
                         DONT_HOP);

    auto const it =
        std::ranges::find_if(m_state->matchRules, [&rule](MatchRuleInfo const& elem) { return elem.rule == rule; });
    if (it != m_state->matchRules.end())
    {
      m_state->matchRules.erase(it);
    }

    co_return;
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::RemoveMatchRule(DBusMatchRule rule)
  {
    if constexpr (SingleThreaded)
    {
      co_await RemoveMatchRuleImpl(std::move(rule));
    }
    else
    {
      co_await boost::asio::co_spawn(m_state->activeContext, RemoveMatchRuleImpl(std::move(rule)),
                                     boost::asio::use_awaitable);
    }
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::RemoveMatchRule(DBusMatchRule rule, DontHopTag)
  {
    co_return co_await RemoveMatchRuleImpl(std::move(rule));
  }

  template <bool SingleThreaded>
  void DBusConnectionImpl<SingleThreaded>::AddMatchRuleSync(
      DBusMatchRule rule, std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback)
  {
    WaitOnAsyncWork<void>(*m_state->strand,
                          [this, rule = std::move(rule), callback = std::move(callback)] -> boost::asio::awaitable<void>
                          { co_return co_await AddMatchRuleImpl(std::move(rule), std::move(callback), true); });
  }

  template <bool SingleThreaded>
  void DBusConnectionImpl<SingleThreaded>::RemoveMatchRuleSync(DBusMatchRule rule)
  {
    WaitOnAsyncWork<void>(*m_state->strand, [this, rule = std::move(rule)] -> boost::asio::awaitable<void>
                          { co_return co_await RemoveMatchRuleImpl(std::move(rule)); });
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::RegisterObjectPathHandlerImpl(
      ObjectPath path, std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback)
  {
    if (auto it = m_state->objectPathHandlers.find(path.GetPath()); it != m_state->objectPathHandlers.end())
    {
      it->second.push_back(std::move(callback));
    }
    else
    {
      m_state->objectPathHandlers.emplace(path.GetPath(), std::vector{std::move(callback)});
    }

    co_return;
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::RegisterObjectPathHandler(
      ObjectPath path, std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback)
  {
    if constexpr (SingleThreaded)
    {
      co_await RegisterObjectPathHandlerImpl(std::move(path), std::move(callback));
    }
    else
    {
      co_return co_await boost::asio::co_spawn(m_state->activeContext,
                                               RegisterObjectPathHandlerImpl(std::move(path), std::move(callback)),
                                               boost::asio::use_awaitable);
    }
  }

  template <bool SingleThreaded>
  void DBusConnectionImpl<SingleThreaded>::RegisterObjectPathHandlerSync(
      ObjectPath path, std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback)
  {
    WaitOnAsyncWork<void>(*m_state->strand,
                          [this, path = std::move(path), callback = std::move(callback)] -> boost::asio::awaitable<void>
                          { co_return co_await RegisterObjectPathHandlerImpl(std::move(path), std::move(callback)); });
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::UnregisterObjectPathHandlerImpl(ObjectPath path)
  {
    m_state->objectPathHandlers.erase(path.GetPath());
    co_return;
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::UnregisterObjectPathHandler(ObjectPath path)
  {
    if constexpr (SingleThreaded)
    {
      co_await UnregisterObjectPathHandlerImpl(std::move(path));
    }
    else
    {
      co_return co_await boost::asio::co_spawn(m_state->activeContext, UnregisterObjectPathHandlerImpl(std::move(path)),
                                               boost::asio::use_awaitable);
    }
  }

  template <bool SingleThreaded>
  void DBusConnectionImpl<SingleThreaded>::UnregisterObjectPathHandlerSync(ObjectPath path)
  {
    WaitOnAsyncWork<void>(*m_state->strand, [this, path = std::move(path)] -> boost::asio::awaitable<void>
                          { co_return co_await UnregisterObjectPathHandlerImpl(std::move(path)); });
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::RequestWellKnownNameImpl(DBusWellKnownName name)
  {
    {
      if (std::ranges::contains(m_state->wellKnownNames, name))
      {
        throw std::runtime_error{std::format("This connection already owns the name '{}'", name.GetName())};
      }
    }

    IncomingDBusMessage reply = co_await SendMessage(
        DBusMessage::Method("RequestName")
            .Path(ObjectPath{"/org/freedesktop/DBus"})
            .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
            .Destination("org.freedesktop.DBus")
            .Parameter(MultipleCompleteTypes<std::string, uint32_t>{name.GetName(), static_cast<uint32_t>(0x1)}),
        DONT_HOP);

    switch (reply.Get<uint32_t>())
    {
      case 1:
        LOG_DEBUG(LOGGER, "Successfully acquired well-known name '{}'", name.GetName());
        break;
      // [TODO]: Allow user passing flags for the Well-known name.
      case 2:
        LOG_ERROR(LOGGER,
                  "Well-known name '{}' is already owned by another connection and we did "
                  "not ask to replace the name",
                  name.GetName());
        break;
      case 3:
        LOG_ERROR(LOGGER, "The well-known name '{}' already has an owner", name.GetName());
        break;
      case 4:
        LOG_DEBUG(LOGGER, "We're already owner of our well-known name");
        break;
      default:
        LOG_ERROR(LOGGER, "Unknown return value from 'RequestName()': {}", reply.Get<uint32_t>());
        break;
    }

    m_state->wellKnownNames.push_back(name);

    co_return;
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::RequestWellKnownName(DBusWellKnownName name)
  {
    if constexpr (SingleThreaded)
    {
      co_await RequestWellKnownNameImpl(std::move(name));
    }
    else
    {
      co_await boost::asio::co_spawn(m_state->activeContext, RequestWellKnownNameImpl(std::move(name)),
                                     boost::asio::use_awaitable);
    }
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::RequestWellKnownName(DBusWellKnownName name,
                                                                                        DontHopTag)
  {
    co_return co_await RequestWellKnownNameImpl(std::move(name));
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::ReleaseWellKnownNameImpl(DBusWellKnownName name)
  {
    if (!std::ranges::contains(m_state->wellKnownNames, name))
    {
      throw std::runtime_error{std::format("This connection does not own the name '{}'", name.GetName())};
    }

    LOG_TRACE(LOGGER, "Releasing our well-known name '{}'", name.GetName());
    IncomingDBusMessage const ret = co_await SendMessage(DBusMessage::Method("ReleaseName")
                                                             .Path(ObjectPath{"/org/freedesktop/DBus"})
                                                             .Destination("org.freedesktop.DBus")
                                                             .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                                             .Parameter(name.GetName()),
                                                         DONT_HOP);

    uint32_t const res = ret.Get<uint32_t>();
    switch (res)
    {
      case 1:
        LOG_DEBUG(LOGGER, "Successfully released well-known name '{}'", name.GetName());
        break;
      case 2:
        LOG_ERROR(LOGGER, "Well-known name '{}' is not owned by the dbus-daemon", name.GetName());
        break;
      case 3:
        LOG_ERROR(LOGGER, "Well-known name '{}' is not owned by this connection", name.GetName());
        break;
      default:
        LOG_ERROR(LOGGER, "Unknown return value from 'ReleaseName()': {}", res);
        break;
    }

    auto it = std::ranges::remove(m_state->wellKnownNames, name);
    m_state->wellKnownNames.erase(it.begin(), it.end());

    co_return;
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::ReleaseWellKnownName(DBusWellKnownName name)
  {
    if constexpr (SingleThreaded)
    {
      co_await ReleaseWellKnownNameImpl(std::move(name));
    }
    else
    {
      co_await boost::asio::co_spawn(m_state->activeContext, ReleaseWellKnownNameImpl(std::move(name)),
                                     boost::asio::use_awaitable);
    }
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::ReleaseWellKnownName(DBusWellKnownName name,
                                                                                        DontHopTag)
  {
    co_return co_await ReleaseWellKnownNameImpl(std::move(name));
  }

  template <bool SingleThreaded>
  void DBusConnectionImpl<SingleThreaded>::RequestWellKnownNameSync(DBusWellKnownName name)
  {
    WaitOnAsyncWork<void>(*m_state->strand, [this, name = std::move(name)] -> boost::asio::awaitable<void>
                          { return RequestWellKnownNameImpl(std::move(name)); });
  }

  template <bool SingleThreaded>
  void DBusConnectionImpl<SingleThreaded>::ReleaseWellKnownNameSync(DBusWellKnownName name)
  {
    WaitOnAsyncWork<void>(*m_state->strand,
                          [this, name = std::move(name)] { return ReleaseWellKnownNameImpl(std::move(name)); });
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<uint32_t> DBusConnectionImpl<SingleThreaded>::RegisterMessageFilter(
      std::function<boost::asio::awaitable<MessageHandled>(IncomingDBusMessage const&)> callback)
  {
    uint32_t filterID = m_state->messageFilterID++;
    m_state->messageFilters.emplace(filterID, std::move(callback));
    co_return filterID;
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::UnregisterMessageFilter(uint32_t filterID)
  {
    m_state->messageFilters.erase(filterID);
    co_return;
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::ReceiveIncomingMessages(
      std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback)
  {
    m_state->onIncomingSignal.push_back(std::move(callback));
    co_return;
  }

  template <bool SingleThreaded>
  std::vector<DBusWellKnownName> const& DBusConnectionImpl<SingleThreaded>::GetWellKnownNames() const
  {
    std::unique_lock<std::mutex> lock{m_state->mutex};
    return m_state->wellKnownNames;
  }

  template <bool SingleThreaded>
  bool DBusConnectionImpl<SingleThreaded>::IsConnected() const
  {
    return m_state->connectionReady;
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<boost::signals2::connection> DBusConnectionImpl<SingleThreaded>::OnDisconnected(
      std::function<void()> callback)
  {
    co_return m_state->onDisconnected.connect(std::move(callback));
  }

  template <bool SingleThreaded>
  void DBusConnectionImpl<SingleThreaded>::SimulateConnectionLoss()
  {
    LOG_TRACE(LOGGER, "Simulating loss of the connection to the dbus-daemon");

    if (m_state->socket.is_open())
    {
      // Hard shutdown the socket, this should cause the HandleConnectionLost() function to get called
      boost::system::error_code ec;
      std::ignore = m_state->socket.shutdown(boost::asio::local::stream_protocol::socket::shutdown_both, ec);
      co_return;
    }
  }

  template <bool SingleThreaded>
  boost::asio::awaitable<void> DBusConnectionImpl<SingleThreaded>::ReadLoop()
  {
    std::vector<byte> rawFullReply{};
    rawFullReply.reserve(1028);
    std::shared_ptr<InternalState> state = m_state;

    while (!state->shouldQuit)
    {
      try
      {
        rawFullReply.clear();

        // Read in the set 12 bytes of the DBus header + the 4 bytes of the following header field array
        co_await boost::asio::async_read(state->socket, boost::asio::dynamic_buffer(rawFullReply),
                                         boost::asio::transfer_exactly(FIRST_HEADER_PART_SIZE),
                                         boost::asio::use_awaitable);

        MultipleCompleteTypes<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t, uint32_t> headerData =
            UnmarshalDBusType<MultipleCompleteTypes<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t, uint32_t>>(
                rawFullReply, "yyyyuuu");
        uint32_t const messageLength = headerData.GetType<4>();
        uint32_t const headerFieldArrLength = headerData.GetType<6>();
        uint32_t const serial = headerData.GetType<5>();
        DBusMessageType const messageType = static_cast<DBusMessageType>(headerData.GetType<1>());

        // Now, read the rest of the message, this is the array length + padding + messageLength
        uint32_t size{FIRST_HEADER_PART_SIZE + headerFieldArrLength};
        uint32_t const nrOfPaddingBytes = AddPaddingToSize(size, DBUS_MESSAGE_BODY_ALIGNMENT);
        co_await boost::asio::async_read(state->socket, boost::asio::dynamic_buffer(rawFullReply),
                                         boost::asio::transfer_exactly(size - FIRST_HEADER_PART_SIZE + messageLength),
                                         boost::asio::use_awaitable);

        // Skip over the padding, we don't care about it
        IncomingDBusMessage message{
            DBusMessageHeader{
                std::span<byte const>{rawFullReply.begin(),
                                      rawFullReply.begin() + FIRST_HEADER_PART_SIZE + headerFieldArrLength},
                serial, messageType, headerFieldArrLength, messageLength},
            std::ranges::to<std::vector>(
                rawFullReply | std::views::drop(FIRST_HEADER_PART_SIZE + headerFieldArrLength + nrOfPaddingBytes))};
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
        LOG_ERROR(LOGGER, "Read loop lost connection to dbus-daemon: {}", ex.what());
        boost::asio::co_spawn(state->activeContext, HandleConnectionLost(), boost::asio::detached);
        break;
      }
      catch (std::exception const& ex)
      {
        LOG_ERROR(LOGGER, "Error occured in message read loop: {}", ex.what());
      }
    }

    LOG_TRACE(LOGGER, "Read Loop is quitting gracefully");
    state->readLoopFinished.async_send(boost::system::error_code{}, boost::asio::detached);
  }
}  // namespace cxxbus
