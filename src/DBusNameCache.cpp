#include "DBusNameCache.h"
#include <boost/asio/awaitable.hpp>
#include <functional>
#include <variant>

#include "DBusConnection.h"
#include "SyncDBusConnection.h"
#include "DBusMatchRule.h"
#include "DBusTypes.h"
#include "Log.h"

namespace cxxbus
{
  namespace
  {
#ifndef CXX_BUS_LOGLEVEL
#define CXX_BUS_LOGLEVEL Error
#endif  // CXX_BUS_LOGLEVEL

    Logger const LOGGER{.logLevel = LogLevel::CXX_BUS_LOGLEVEL};
  }

  DBusNameCache::DBusNameCache(DBusConnection& conn)
    : m_conn(conn)
    , m_wellKnownNames()
  {
  }

  DBusNameCache::DBusNameCache(SyncDBusConnection& conn)
    : m_conn(conn)
    , m_wellKnownNames()
  {
  }

  void DBusNameCache::SubscribeToNameChangesSync()
  {
    if (std::holds_alternative<std::reference_wrapper<DBusConnection>>(m_conn))
    {
      LOGGER.LogFatal(
          "SubscribeToNameChangesSync() should not be called on a DBusConnection, use SubscribeToNameChanges() "
          "instead");
      throw InternalError{
          "SubscribeToNameChangesSync() should not be called on a DBusConnection, use SubscribeToNameChanges() "
          "instead"};
    }

    std::get<std::reference_wrapper<SyncDBusConnection>>(m_conn).get().AddMatchRule(
        DBusMatchRule::Create()
            .Sender(DBusWellKnownName{"org.freedesktop.DBus"})
            .Path(ObjectPath{"/org/freedesktop/DBus"})
            .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
            .Member("NameOwnerChanged"),
        [this](IncomingDBusMessage message) { OnNameOwnerChanged(std::move(message)); });
  }

  boost::asio::awaitable<void> DBusNameCache::SubscribeToNameChanges()
  {
    if (std::holds_alternative<std::reference_wrapper<SyncDBusConnection>>(m_conn))
    {
      LOGGER.LogFatal(
          "SubscribeToNameChanges() should not be called on a SyncDBusConnection, use SubscribeToNameChangesSync() "
          "instead");
      throw InternalError{
          "SubscribeToNameChanges() should not be called on a SyncDBusConnection, use SubscribeToNameChangesSync() "
          "instead"};
    }

    co_await std::get<std::reference_wrapper<DBusConnection>>(m_conn).get().AddMatchRule(
        DBusMatchRule::Create()
            .Sender(DBusWellKnownName{"org.freedesktop.DBus"})
            .Path(ObjectPath{"/org/freedesktop/DBus"})
            .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
            .Member("NameOwnerChanged"),
        [this](IncomingDBusMessage message) -> boost::asio::awaitable<void>
        {
          OnNameOwnerChanged(std::move(message));
          co_return;
        });
  }

  void DBusNameCache::OnNameOwnerChanged(IncomingDBusMessage message)
  {
    MultipleCompleteTypes<std::string, std::string, std::string> const parameters{message.Get<MultipleCompleteTypes<std::string, std::string, std::string>>()};

    LOGGER.LogTrace(
        std::format("NameOwnerChanged signal triggered: '{}', '{}', '{}'", parameters.GetType<0>(), parameters.GetType<1>(), parameters.GetType<2>()));

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