#include "DBusNameCache.h"

#include "DBusConnection.h"
#include "DBusMatchRule.h"
#include "DBusTypes.h"
#include "Log.h"

namespace cxxbus
{
  namespace
  {
    Logger const LOGGER{.logLevel = LogLevel::Trace};
  }

  DBusNameCache::DBusNameCache(DBusConnection& conn)
    : m_conn(conn)
    , m_wellKnownNames()
  {
  }

  void DBusNameCache::SubscribeToNameChangesSync()
  {
    m_conn.AddMatchRuleSync(DBusMatchRule::Create()
                                .Sender(DBusWellKnownName{"org.freedesktop.DBus"})
                                .Path(ObjectPath{"/org/freedesktop/DBus"})
                                .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                .Member("NameOwnerChanged"),
                            [this](IncomingDBusMessage message) { OnNameOwnerChanged(std::move(message)); });
  }

  boost::asio::awaitable<void> DBusNameCache::SubscribeToNameChanges()
  {
    co_await m_conn.AddMatchRule(DBusMatchRule::Create()
                                     .Sender(DBusWellKnownName{"org.freedesktop.DBus"})
                                     .Path(ObjectPath{"/org/freedesktop/DBus"})
                                     .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                     .Member("NameOwnerChanged"),
                                 [this](IncomingDBusMessage message) { OnNameOwnerChanged(std::move(message)); });
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
    std::vector<std::string> wellKnownNames{};
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