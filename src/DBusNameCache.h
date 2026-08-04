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
    // Uses `std::string` instead of `DBusUniqueConnectionName` as parameter type because the sender of a message is not
    // guaranteed to be present nor a valid unique connection name.
    std::vector<std::string> GetWellKnownNames(std::string const& uniqueName) const;
  };
}  // namespace cxxbus
