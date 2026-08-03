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
#include "DBusConnection.h"

namespace cxxbus
{
  class DBusConnection;

  enum class DispatchStatus : uint8_t
  {
    DISPATCH_PENDING,
    COMPLETED
  };

  class SyncDBusConnection
  {
   private:
    friend class DBusConnection;

   public:
    struct SyncMessageToSendInfo
    {
      DBusMessage message;
      uint32_t serial;
      std::shared_ptr<std::promise<IncomingDBusMessage>> promise;
      std::shared_future<IncomingDBusMessage> future;
    };

    struct InternalState
    {
      // Gotten from DBusConnection
      std::shared_ptr<boost::asio::local::stream_protocol::socket> socket;
      std::shared_ptr<DBusUniqueConnectionName> uniqueConnection;
      std::shared_ptr<DBusWellKnownName> wellKnownName;
      std::shared_ptr<uint32_t> serial;
      std::shared_ptr<uint32_t> subscriptionCounter;
      std::shared_ptr<std::unordered_map<uint32_t, DBusConnection::MatchRuleInfo>> matchRules;
      std::shared_ptr<DBusNameCache> nameCache;

      // Information to send back to DBusConnection
      std::shared_ptr<std::queue<IncomingDBusMessage>> unhandledIncomingMessages;
    };

   private:
    std::shared_ptr<InternalState> m_state;

   private:
    SyncDBusConnection(DBusConnection & dbusConnection);

    bool HandleReadMessage(IncomingDBusMessage message, uint32_t expectedReplySerial);

   public:
    static std::shared_ptr<SyncDBusConnection> Create(DBusConnection & dbusConnection);

    void AddMatchRule(DBusMatchRule rule,  std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback);
    void RemoveMatchRule(DBusMatchRule rule);

    IncomingDBusMessage SendMessage(DBusMessage message);
    void SendMessageNoReply(DBusMessage message);

    DBusWellKnownName const& GetWellKnownName() const;
  };
}  // namespace cxxbus
