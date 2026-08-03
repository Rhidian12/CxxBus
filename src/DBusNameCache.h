#pragma once

#include <boost/asio/awaitable.hpp>
#include <set>
#include <unordered_map>

#include "IncomingDBusMessage.h"

namespace cxxbus
{
  class DBusConnection;
  class SyncDBusConnection;

  class DBusNameCache
  {
   private:
    DBusConnection& m_conn;
    std::unordered_map<std::string, std::set<std::string>> m_wellKnownNames;

   private:
    void OnNameOwnerChanged(IncomingDBusMessage message);

   public:
    DBusNameCache(DBusConnection& conn);

    boost::asio::awaitable<void> SubscribeToNameChanges();

    // Returns a list of well-known names associated with the given unique connection name.
    // Uses `std::string` instead of `DBusUniqueConnectionName` as parameter type because the sender of a message is not guaranteed to be
    // present nor a valid unique connection name.
    std::vector<std::string> GetWellKnownNames(std::string const& uniqueName) const;
  };
}  // namespace cxxbus