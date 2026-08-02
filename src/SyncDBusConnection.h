#pragma once

#include <unistd.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/signals2.hpp>
#include <boost/system/detail/error_code.hpp>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <queue>
#include <unordered_map>

#include "DBusMatchRule.h"
#include "DBusMessage.h"
#include "DBusNameCache.h"
#include "DBusTypes.h"
#include "IncomingDBusMessage.h"
#include "Log.h"

namespace cxxbus
{
  enum class DispatchStatus : uint8_t
  {
    DISPATCH_PENDING,
    COMPLETED
  };

  class SyncDBusConnection
  {
   public:
    struct SyncMessageToSendInfo
    {
      DBusMessage message;
      uint32_t serial;
      std::shared_ptr<std::promise<IncomingDBusMessage>> promise;
      std::shared_future<IncomingDBusMessage> future;
    };

   private:
    struct MatchRuleInfo
    {
      DBusMatchRule rule;
      std::function<void(IncomingDBusMessage const&)> callback;
    };

    struct InternalState
    {
      boost::asio::local::stream_protocol::socket socket;

      std::unordered_map<std::string, boost::signals2::signal<void(IncomingDBusMessage const&)>> objectPathHandlers;
      boost::signals2::signal<void(IncomingDBusMessage const&)> onIncomingSignal;

      uint32_t serial;
      std::string uniqueConnection;
      DBusWellKnownName wellKnownName;

      uint32_t subscriptionCounter;
      std::unordered_map<uint32_t, MatchRuleInfo> matchRules;

      DBusNameCache nameCache;
      std::queue<IncomingDBusMessage> messagesToDispatch;

      std::function<void(DispatchStatus)> dispatchHandler;

      std::function<void(boost::asio::local::stream_protocol::socket&)> pollHandler;

      Logger logger;
    };

   private:
    std::shared_ptr<InternalState> m_state;

   private:
    void Connect();
    void CloseData();
    bool HandleReadMessage(IncomingDBusMessage message);

   private:
    SyncDBusConnection(boost::asio::io_context& ioContext, DBusWellKnownName wellKnownName);

   public:
    ~SyncDBusConnection();
    void Close();

    static std::shared_ptr<SyncDBusConnection> Create(boost::asio::io_context& ioContext,
                                                      DBusWellKnownName wellKnownName);

    // Receive messages on a specific object path
    void RegisterObjectPathHandler(ObjectPath path, std::function<void(IncomingDBusMessage const&)> callback);
    void ReceiveIncomingMessages(std::function<void(IncomingDBusMessage const&)> callback);

    void AddMatchRule(DBusMatchRule rule, std::function<void(IncomingDBusMessage const&)> callback);
    void RemoveMatchRule(DBusMatchRule rule);

    IncomingDBusMessage SendMessage(DBusMessage message);
    void SendMessageNoReply(DBusMessage message);

    void SetDispatchHandler(std::function<void(DispatchStatus)> callback);
    DispatchStatus DispatchIncomingMessages();

    void SetPollHandler(std::function<void(boost::asio::local::stream_protocol::socket&)> callback);
    void Poll();

    DBusWellKnownName const& GetWellKnownName() const;

    void SetLogLevel(LogLevel logLevel);
  };
}  // namespace cxxbus
