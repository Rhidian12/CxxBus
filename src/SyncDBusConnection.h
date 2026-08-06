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
#include <memory>

#include "DBusConnection.h"
#include "DBusMatchRule.h"
#include "DBusMessage.h"
#include "DBusNameCache.h"
#include "DBusTypes.h"
#include "IncomingDBusMessage.h"

namespace cxxbus
{
  class DBusConnection;

  class SyncDBusConnection
  {
   private:
    friend class DBusConnection;

   private:
    std::shared_ptr<DBusConnection::InternalState> m_state;
    boost::asio::io_context& m_ioContext;

   private:
    SyncDBusConnection(DBusConnection& dbusConnection);

    // Queues a message onto 'DBusConnection' its SendLoop() coroutine and blocks until the message has been sent to the wire.
    void DispatchMessage(DBusMessage message, uint32_t serial);

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
