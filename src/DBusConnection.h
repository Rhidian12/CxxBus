#pragma once

#include <unistd.h>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/signals2.hpp>
#include <boost/system/detail/error_code.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>

#include "DBusMatchRule.h"
#include "DBusMessage.h"
#include "DBusNameCache.h"
#include "DBusTypes.h"
#include "IncomingDBusMessage.h"
#include "AwaitableSignal.h"

namespace cxxbus
{
  enum class CreateConnectionDetached : uint8_t
  {
    NO = 0,
    YES = 1,
  };

  class DBusConnection : public std::enable_shared_from_this<DBusConnection>
  {
   private:
    struct MatchRuleInfo
    {
      DBusMatchRule rule;
      std::shared_ptr<AwaitableSignal<void, IncomingDBusMessage>> callback;
      std::function<void(IncomingDBusMessage)> callbackSync;
    };

    struct InternalState
    {
      boost::asio::local::stream_protocol::socket socket;

      // Store channels to make our 'SendMessage' be awaitable
      std::map<uint32_t, boost::asio::experimental::channel<void(boost::system::error_code, IncomingDBusMessage)>*> replyChannels;
      // Same as above, but for our sync version
      std::map<uint32_t, std::function<void(IncomingDBusMessage)>> replySyncCallbacks;

      std::unordered_map<std::string, AwaitableSignal<void, IncomingDBusMessage>> objectPathHandlers;
      AwaitableSignal<void, IncomingDBusMessage> onIncomingSignal;

      // Send messages to the SendLoop() coroutine
      boost::asio::experimental::channel<void(
          boost::system::error_code, std::tuple<DBusMessage /* message */, uint32_t /* serial */,
                                                std::shared_ptr<boost::asio::experimental::channel<void(boost::system::error_code)>> /* messageSentChannel */>)>
          sendLoop;

      bool connectionReady;
      boost::asio::experimental::channel<void(boost::system::error_code)> connectionCompleted;
      int nrOfWaiters;  // Number of coroutines waiting for the connection to be ready

      uint32_t serial;
      std::string uniqueConnection;
      DBusWellKnownName wellKnownName;

      uint32_t subscriptionCounter;
      std::unordered_map<uint32_t, MatchRuleInfo> matchRules;

      DBusNameCache nameCache;
    };

   private:
    boost::asio::io_context& m_ioContext;
    std::shared_ptr<InternalState> m_state;

   private:
    boost::asio::awaitable<void> AuthenticateDBusConnection();
    boost::asio::awaitable<void> Connect();
    boost::asio::awaitable<void> SendLoop();
    boost::asio::awaitable<void> ReadLoop();

    void AuthenticateDBusConnectionSync();
    void ConnectSync();

    void CloseData();

   private:
    DBusConnection(boost::asio::io_context& ioService, DBusWellKnownName wellKnownName);

    // Does not wait for the connection to be ready -> Can be used internally to set up the connection.
    // Prefer 'SendMessage()' whenever possible
    boost::asio::awaitable<std::optional<IncomingDBusMessage>> SendMessageInternal(DBusMessage message);
    std::optional<IncomingDBusMessage> SendMessageInternalSync(DBusMessage message);

   public:
    ~DBusConnection();
    boost::asio::awaitable<void> Close();
    void CloseSync();

    static boost::asio::awaitable<std::shared_ptr<DBusConnection>> Create(boost::asio::io_context& ioService, DBusWellKnownName wellKnownName,
                                                                          CreateConnectionDetached connectionMethod);
    static std::shared_ptr<DBusConnection> CreateSync(boost::asio::io_context& ioService, DBusWellKnownName wellKnownName);

    // Receive messages on a specific object path
    void RegisterObjectPathHandler(ObjectPath path, std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback);
    void ReceiveIncomingMessages(std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback);

    boost::asio::awaitable<void> AddMatchRule(DBusMatchRule rule, std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback);
    boost::asio::awaitable<void> RemoveMatchRule(DBusMatchRule rule);

    void AddMatchRuleSync(DBusMatchRule rule, std::function<void(IncomingDBusMessage)> callback);
    void RemoveMatchRuleSync(DBusMatchRule rule);

    boost::asio::awaitable<IncomingDBusMessage> SendMessage(DBusMessage message);
    boost::asio::awaitable<void> SendMessageNoReply(DBusMessage message);

    IncomingDBusMessage SendMessageSync(DBusMessage message);
    void SendMessageNoReplySync(DBusMessage message);

    DBusWellKnownName const& GetWellKnownName() const;
  };
}  // namespace cxxbus