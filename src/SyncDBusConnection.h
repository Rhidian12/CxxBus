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

#include "DBusConnection.h"
#include "DBusMatchRule.h"
#include "DBusMessage.h"
#include "DBusNameCache.h"
#include "DBusTypes.h"
#include "IncomingDBusMessage.h"

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
      std::shared_ptr<std::vector<DBusWellKnownName>> wellKnownNames;
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
    SyncDBusConnection(DBusConnection& dbusConnection);

    bool HandleReadMessage(IncomingDBusMessage message, uint32_t expectedReplySerial);

   public:
    static std::shared_ptr<SyncDBusConnection> Create(DBusConnection& dbusConnection);

    void AddMatchRule(DBusMatchRule rule, std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback);
    void RemoveMatchRule(DBusMatchRule rule);

    IncomingDBusMessage SendMessage(DBusMessage message);
    void SendMessageNoReply(DBusMessage message);

    void RequestWellKnownName(DBusWellKnownName name);
    void ReleaseWellKnownName(DBusWellKnownName name);

    std::vector<DBusWellKnownName> const& GetWellKnownNames() const;
  };
}  // namespace cxxbus
