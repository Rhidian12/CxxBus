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
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/strand.hpp>
#include <boost/signals2.hpp>
#include <boost/system/detail/error_code.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "DBusMatchRule.h"
#include "DBusMessage.h"
#include "DBusNameCache.h"
#include "DBusTypes.h"
#include "IncomingDBusMessage.h"

namespace cxxbus
{
#define CXX_BUS_MAX_CONCURRENT_MESSAGES 256

  enum class MessageHandled
  {
    YES,
    NO
  };

  template <bool SingleThreaded /* = true */>
  class DBusConnectionImpl : public std::enable_shared_from_this<DBusConnectionImpl<SingleThreaded>>
  {
   private:
    friend class DBusNameCache<SingleThreaded>;

   public:
    struct MatchRuleInfo
    {
      DBusMatchRule rule;
      std::vector<std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)>> callback;
      bool executeOnUserContext;
    };

    struct ChannelInfo
    {
      boost::asio::experimental::channel<void(boost::system::error_code, IncomingDBusMessage)> channel;
      bool ready;
    };

    struct DontHopTag
    {
    };
    constexpr static DontHopTag DONT_HOP{};

   private:
    struct InternalState
    {
      std::shared_ptr<boost::asio::io_context> ioContext;

      // Store channels to make our 'SendMessage' be awaitable
      // std::map<uint32_t, boost::asio::experimental::channel<void(boost::system::error_code, IncomingDBusMessage)>*>
      //     replyChannels;
      std::vector<ChannelInfo> replyChannels;

      std::vector<std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)>> onIncomingSignal;
      std::unordered_map<uint32_t, std::function<boost::asio::awaitable<MessageHandled>(IncomingDBusMessage const&)>>
          messageFilters;
      uint32_t messageFilterID;

      boost::signals2::signal<void()> onDisconnected;

      bool connectionReady;
      boost::asio::experimental::channel<void(boost::system::error_code)> connectionCompleted;
      int nrOfWaiters;  // Number of coroutines waiting for the connection to be ready

      std::unique_ptr<boost::asio::strand<typename boost::asio::io_context::executor_type>> strand;
      boost::asio::local::stream_protocol::socket socket;
      std::optional<DBusUniqueConnectionName> uniqueConnection;
      std::vector<DBusWellKnownName> wellKnownNames;
      uint32_t serial;
      std::vector<MatchRuleInfo> matchRules;
      std::shared_ptr<DBusNameCache<SingleThreaded>> nameCache;
      std::unordered_map<std::string,
                         std::vector<std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)>>>
          objectPathHandlers;

      // Thread Info
      std::mutex mutex;
      std::unique_ptr<boost::asio::executor_work_guard<typename boost::asio::io_context::executor_type>> workGuard;
      std::shared_ptr<std::thread> ioThread;

      boost::asio::any_io_executor activeContext;

      bool shouldQuit;
      boost::asio::experimental::channel<void(boost::system::error_code)> readLoopFinished;
    };

   private:
    std::shared_ptr<InternalState> m_state;
    boost::asio::io_context& m_userIOContext;

   private:
    boost::asio::awaitable<void> AuthenticateDBusConnectionImpl();
    boost::asio::awaitable<void> Connect(BusType busType);
    boost::asio::awaitable<void> SendLoop();
    boost::asio::awaitable<void> ReadLoop();
    boost::asio::awaitable<void> HandleReadMessage(IncomingDBusMessage&& message);

    boost::asio::awaitable<void> CloseData();

    boost::asio::awaitable<void> HandleConnectionLost();

   private:
    DBusConnectionImpl(boost::asio::io_context& ioService, std::optional<DBusWellKnownName> wellKnownName);

    // Does not wait for the connection to be ready -> Can be used internally to set up the connection.
    // Prefer 'SendMessage()' whenever possible
    boost::asio::awaitable<std::optional<IncomingDBusMessage>> SendMessageInternal(DBusMessage&& message);

    boost::asio::awaitable<IncomingDBusMessage> SendMessageImpl(DBusMessage message);
    boost::asio::awaitable<void> SendMessageNoReplyImpl(DBusMessage message);
    boost::asio::awaitable<void> RequestWellKnownNameImpl(DBusWellKnownName name);
    boost::asio::awaitable<void> ReleaseWellKnownNameImpl(DBusWellKnownName name);
    boost::asio::awaitable<void> AddMatchRuleImpl(
        DBusMatchRule rule, std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback,
        bool executeOnUserContext);
    boost::asio::awaitable<void> RemoveMatchRuleImpl(DBusMatchRule rule);
    boost::asio::awaitable<void> CloseImpl();
    boost::asio::awaitable<void> RegisterObjectPathHandlerImpl(
        ObjectPath path, std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback);
    boost::asio::awaitable<void> UnregisterObjectPathHandlerImpl(ObjectPath path);

    boost::asio::awaitable<IncomingDBusMessage> SendMessage(DBusMessage message, DontHopTag);
    boost::asio::awaitable<void> AddMatchRule(
        DBusMatchRule rule, std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback,
        DontHopTag);
    boost::asio::awaitable<void> AddMatchRule(
        DBusMatchRule rule, std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback,
        bool executeOnUserContext);
    boost::asio::awaitable<void> RemoveMatchRule(DBusMatchRule rule, DontHopTag);
    boost::asio::awaitable<void> RequestWellKnownName(DBusWellKnownName name, DontHopTag);
    boost::asio::awaitable<void> ReleaseWellKnownName(DBusWellKnownName name, DontHopTag);
    boost::asio::awaitable<void> Close(DontHopTag);

   public:
    ~DBusConnectionImpl();
    boost::asio::awaitable<void> Close();
    void CloseSync();

    static boost::asio::awaitable<std::shared_ptr<DBusConnectionImpl>> Create(
        boost::asio::io_context& ioService, std::optional<DBusWellKnownName> wellKnownName, BusType busType);
    static std::shared_ptr<DBusConnectionImpl> CreateDetached(
        boost::asio::io_context& ioService, std::optional<DBusWellKnownName> wellKnownName,
        std::function<boost::asio::awaitable<void>()> onConnectedCallback, BusType busType);
    static std::shared_ptr<DBusConnectionImpl> CreateSync(boost::asio::io_context& ioService,
                                                          std::optional<DBusWellKnownName> wellKnownName,
                                                          BusType busType);

    // Receive messages on a specific object path
    boost::asio::awaitable<void> RegisterObjectPathHandler(
        ObjectPath path, std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback);
    boost::asio::awaitable<void> UnregisterObjectPathHandler(ObjectPath path);
    void RegisterObjectPathHandlerSync(
        ObjectPath path, std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback);
    void UnregisterObjectPathHandlerSync(ObjectPath path);
    // Register a filter that will filter incoming messages before dispatching them to object path handlers
    boost::asio::awaitable<uint32_t> RegisterMessageFilter(
        std::function<boost::asio::awaitable<MessageHandled>(IncomingDBusMessage const&)> callback);
    boost::asio::awaitable<void> UnregisterMessageFilter(uint32_t filterID);

    boost::asio::awaitable<void> ReceiveIncomingMessages(
        std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback);

    boost::asio::awaitable<void> AddMatchRule(
        DBusMatchRule rule, std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback);
    boost::asio::awaitable<void> RemoveMatchRule(DBusMatchRule rule);

    void AddMatchRuleSync(DBusMatchRule rule,
                          std::function<boost::asio::awaitable<void>(IncomingDBusMessage const&)> callback);
    void RemoveMatchRuleSync(DBusMatchRule rule);

    boost::asio::awaitable<IncomingDBusMessage> SendMessage(DBusMessage&& message);
    boost::asio::awaitable<IncomingDBusMessage> SendMessage(DBusMessage const& message);
    boost::asio::awaitable<void> SendMessageNoReply(DBusMessage message);

    IncomingDBusMessage SendMessageSync(DBusMessage message);
    void SendMessageNoReplySync(DBusMessage message);

    boost::asio::awaitable<void> RequestWellKnownName(DBusWellKnownName name);
    boost::asio::awaitable<void> ReleaseWellKnownName(DBusWellKnownName name);

    void RequestWellKnownNameSync(DBusWellKnownName name);
    void ReleaseWellKnownNameSync(DBusWellKnownName name);

    std::vector<DBusWellKnownName> const& GetWellKnownNames() const;

    bool IsConnected() const;

    boost::asio::awaitable<boost::signals2::connection> OnDisconnected(std::function<void()> callback);

    // This is a hack to simulate a connection loss for testing purposes
    void SimulateConnectionLoss();
  };

  using DBusConnection = DBusConnectionImpl<true>;
  using MultithreadedDBusConnection = DBusConnectionImpl<false>;
}  // namespace cxxbus

#include "DBusConnection.txx"
