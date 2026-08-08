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

#include "DBusNameCache.h"

#include <boost/asio/awaitable.hpp>
#include <functional>
#include <variant>

#include "DBusConnection.h"
#include "DBusMatchRule.h"
#include "DBusTypes.h"
#include "Log.h"
#include "SyncDBusConnection.h"

namespace cxxbus
{
  namespace
  {
#ifndef CXX_BUS_LOGLEVEL
#define CXX_BUS_LOGLEVEL Error
#endif  // CXX_BUS_LOGLEVEL

    Logger const LOGGER{.logLevel = LogLevel::CXX_BUS_LOGLEVEL};
  }  // namespace

  DBusNameCache::DBusNameCache(DBusConnection& conn)
    : m_conn(conn)
    , m_wellKnownNames()
  {
  }

  boost::asio::awaitable<void> DBusNameCache::SubscribeToNameChanges(boost::asio::io_context& ioContext)
  {
    co_await m_conn.AddMatchRule(
        DBusMatchRule::Create()
            .Sender(DBusWellKnownName{"org.freedesktop.DBus"})
            .Path(ObjectPath{"/org/freedesktop/DBus"})
            .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
            .Member("NameOwnerChanged"),
        [this](IncomingDBusMessage message) -> boost::asio::awaitable<void>
        {
          OnNameOwnerChanged(std::move(message));
          co_return;
        },
        ioContext);
  }

  void DBusNameCache::OnNameOwnerChanged(IncomingDBusMessage message)
  {
    MultipleCompleteTypes<std::string, std::string, std::string> const parameters{
        message.Get<MultipleCompleteTypes<std::string, std::string, std::string>>()};

    LOGGER.LogTrace(std::format("NameOwnerChanged signal triggered: '{}', '{}', '{}'", parameters.GetType<0>(),
                                parameters.GetType<1>(), parameters.GetType<2>()));

    std::string const wellKnownName{parameters.GetType<0>()};
    std::string const oldUniqueName{parameters.GetType<1>()};
    std::string const uniqueName{parameters.GetType<2>()};

    if (!oldUniqueName.empty())
    {
      if (m_wellKnownNames.contains(oldUniqueName))
      {
        if (m_wellKnownNames[oldUniqueName].contains(wellKnownName))
        {
          m_wellKnownNames[oldUniqueName].erase(wellKnownName);
        }

        if (m_wellKnownNames[oldUniqueName].empty())
        {
          m_wellKnownNames.erase(oldUniqueName);
        }
      }
    }

    if (!uniqueName.empty())
    {
      m_wellKnownNames[uniqueName].insert(std::move(wellKnownName));
    }
  }

  std::vector<std::string> DBusNameCache::GetWellKnownNames(std::string const& uniqueName) const
  {
    // Add the uniqueName itself as it's a valid sender and we might not have gotten any other names so far
    // If we don't do this we might not be able to match signals from a sender that has no well-known name yet
    std::vector<std::string> wellKnownNames{uniqueName};

    auto const it = m_wellKnownNames.find(uniqueName);
    if (it != m_wellKnownNames.cend())
    {
      for (std::string const& wellKnownName : it->second)
      {
        wellKnownNames.push_back(wellKnownName);
      }
    }

    return wellKnownNames;
  }
}  // namespace cxxbus
