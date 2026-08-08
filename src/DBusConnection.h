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

#include <unistd.h>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/signals2.hpp>
#include <boost/system/detail/error_code.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <unordered_map>

#include "AwaitableSignal.h"
#include "DBusMatchRule.h"
#include "DBusMessage.h"
#include "DBusNameCache.h"
#include "DBusTypes.h"
#include "IncomingDBusMessage.h"

namespace cxxbus
{
  class SyncDBusConnection;

  enum class MessageHandled
  {
    YES,
    NO
  };

  class DBusConnection : public std::enable_shared_from_this<DBusConnection>
  {
   private:
    friend class SyncDBusConnection;

   public:
    struct MatchRuleInfo
    {
      DBusMatchRule rule;
      std::shared_ptr<AwaitableSignal<void, IncomingDBusMessage>> callback;
      std::shared_ptr<boost::signals2::signal<void(IncomingDBusMessage)>> syncCallback;
    };

   private:
    struct InternalState
    {
      std::shared_ptr<boost::asio::io_context> ioContext;

      // Store channels to make our 'SendMessage' be awaitable
      std::map<uint32_t, boost::asio::experimental::channel<void(boost::system::error_code, IncomingDBusMessage)>*>
          replyChannels;

      AwaitableSignal<void, IncomingDBusMessage> onIncomingSignal;
      std::unordered_map<uint32_t, AwaitableSignal<MessageHandled, IncomingDBusMessage>> messageFilter;
      uint32_t messageFilterID;

      boost::signals2::signal<void()> onDisconnected;

      // Send messages to the SendLoop() coroutine
      boost::asio::experimental::channel<void(boost::system::error_code,
                                              std::tuple<DBusMessage /* message */, uint32_t /* serial */,
                                                         std::shared_ptr<boost::asio::experimental::channel<void(
                                                             boost::system::error_code)>> /* messageSentChannel */>)>
          sendLoop;

      bool connectionReady;
      boost::asio::experimental::channel<void(boost::system::error_code)> connectionCompleted;
      int nrOfWaiters;  // Number of coroutines waiting for the connection to be ready

      std::shared_ptr<boost::asio::strand<typename boost::asio::io_context::executor_type>> strand;
      std::shared_ptr<boost::asio::local::stream_protocol::socket> socket;
      std::shared_ptr<DBusUniqueConnectionName> uniqueConnection;
      std::shared_ptr<std::vector<DBusWellKnownName>> wellKnownNames;
      std::shared_ptr<uint32_t> serial;
      std::shared_ptr<uint32_t> subscriptionCounter;
      std::shared_ptr<std::unordered_map<uint32_t, MatchRuleInfo>> matchRules;
      std::shared_ptr<DBusNameCache> nameCache;
      std::shared_ptr<std::unordered_map<std::string, AwaitableSignal<void, IncomingDBusMessage>>> objectPathHandlers;

      // Thread Info
      std::shared_ptr<std::mutex> mutex;
      std::unique_ptr<boost::asio::executor_work_guard<typename boost::asio::io_context::executor_type>> workGuard;
      std::shared_ptr<std::thread> ioThread;

      // Information gotten from other connections
      std::shared_ptr<std::queue<IncomingDBusMessage>> unhandledIncomingMessages;

      // Information to deal with unhandled messages
      boost::asio::system_timer timer;
      bool shouldQuit;
    };

   private:
    std::shared_ptr<InternalState> m_state;
    boost::asio::io_context& m_userIOContext;

   private:
    boost::asio::awaitable<void> AuthenticateDBusConnection();
    boost::asio::awaitable<void> Connect(BusType busType, boost::asio::io_context& ioContext);
    boost::asio::awaitable<void> SendLoop();
    boost::asio::awaitable<void> ReadLoop();
    boost::asio::awaitable<void> HandleUnhandledIncomingMessages();
    boost::asio::awaitable<void> HandleReadMessage(IncomingDBusMessage message);

    void CloseData();

    void HandleConnectionLost();

   private:
    DBusConnection(boost::asio::io_context& ioService, std::optional<DBusWellKnownName> wellKnownName);

    // Does not wait for the connection to be ready -> Can be used internally to set up the connection.
    // Prefer 'SendMessage()' whenever possible
    boost::asio::awaitable<std::optional<IncomingDBusMessage>> SendMessageInternal(DBusMessage message,
                                                                                   boost::asio::io_context& ioContext);
    std::optional<IncomingDBusMessage> SendMessageInternalSync(DBusMessage message);

   public:
    ~DBusConnection();
    boost::asio::awaitable<void> Close(boost::asio::io_context& ioContext);
    boost::asio::awaitable<void> Close();
    void CloseSync();

    static boost::asio::awaitable<std::shared_ptr<DBusConnection>> Create(
        boost::asio::io_context& ioService, std::optional<DBusWellKnownName> wellKnownName, BusType busType);
    static std::shared_ptr<DBusConnection> CreateDetached(
        boost::asio::io_context& ioService, std::optional<DBusWellKnownName> wellKnownName,
        std::function<boost::asio::awaitable<void>()> onConnectedCallback, BusType busType);
    static std::shared_ptr<DBusConnection> CreateSync(boost::asio::io_context& ioService,
                                                      std::optional<DBusWellKnownName> wellKnownName, BusType busType);

    // Receive messages on a specific object path
    void RegisterObjectPathHandler(ObjectPath path,
                                   std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback);
    void UnregisterObjectPathHandler(ObjectPath path);
    // Register a filter that will filter incoming messages before dispatching them to object path handlers
    uint32_t RegisterMessageFilter(std::function<boost::asio::awaitable<MessageHandled>(IncomingDBusMessage)> callback);
    void UnregisterMessageFilter(uint32_t filterID);

    void ReceiveIncomingMessages(std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback);

    boost::asio::awaitable<void> AddMatchRule(
        DBusMatchRule rule, std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback);
    boost::asio::awaitable<void> AddMatchRule(DBusMatchRule rule,
                                              std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback,
                                              boost::asio::io_context& ioContext);
    boost::asio::awaitable<void> RemoveMatchRule(DBusMatchRule rule);
    boost::asio::awaitable<void> RemoveMatchRule(DBusMatchRule rule, boost::asio::io_context& ioContext);

    void AddMatchRuleSync(DBusMatchRule rule, std::function<void(IncomingDBusMessage)> callback);
    void RemoveMatchRuleSync(DBusMatchRule rule);

    boost::asio::awaitable<IncomingDBusMessage> SendMessage(DBusMessage message);
    boost::asio::awaitable<IncomingDBusMessage> SendMessage(DBusMessage message, boost::asio::io_context& ioContext);
    boost::asio::awaitable<void> SendMessageNoReply(DBusMessage message);
    boost::asio::awaitable<void> SendMessageNoReply(DBusMessage message, boost::asio::io_context& ioContext);

    IncomingDBusMessage SendMessageSync(DBusMessage message);
    void SendMessageNoReplySync(DBusMessage message);

    boost::asio::awaitable<void> RequestWellKnownName(DBusWellKnownName name, boost::asio::io_context& ioContext);
    boost::asio::awaitable<void> RequestWellKnownName(DBusWellKnownName name);
    boost::asio::awaitable<void> ReleaseWellKnownName(DBusWellKnownName name, boost::asio::io_context& ioContext);
    boost::asio::awaitable<void> ReleaseWellKnownName(DBusWellKnownName name);

    void RequestWellKnownNameSync(DBusWellKnownName name);
    void ReleaseWellKnownNameSync(DBusWellKnownName name);

    std::vector<DBusWellKnownName> const& GetWellKnownNames() const;

    bool IsConnected() const;

    boost::signals2::connection OnDisconnected(std::function<void()> callback);

    // This is a hack to simulate a connection loss for testing purposes
    void SimulateConnectionLoss();
  };
}  // namespace cxxbus
