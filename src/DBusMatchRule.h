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

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "DBusTypes.h"
#include "IncomingDBusMessage.h"

namespace cxxbus
{
  class EmptyDBusMatchRule : public std::runtime_error
  {
   public:
    using std::runtime_error::runtime_error;
  };

  class InvalidDBusMatchRule : public std::runtime_error
  {
   public:
    using std::runtime_error::runtime_error;
  };

  class DBusMatchRule
  {
   private:
    struct ArgInfo
    {
      std::string name;
      uint8_t index;

      auto operator<=>(ArgInfo const&) const noexcept = default;
    };

   private:
    std::optional<DBusMessageType> m_messageType;
    std::optional<std::string> m_sender;
    std::optional<DBusInterfaceName> m_interface;
    std::optional<std::string> m_member;
    std::optional<ObjectPath> m_path;
    std::optional<ObjectPath> m_pathNamespace;
    std::optional<DBusUniqueConnectionName> m_destination;
    std::vector<ArgInfo> m_args;
    std::vector<ArgInfo> m_argPaths;
    std::optional<std::string> m_argNamespace;
    std::optional<bool> m_eavesdrop;

   private:
    DBusMatchRule() = default;

   public:
    // The starting point to chain all methods together
    static DBusMatchRule Create();

    // Type of the message to match on
    DBusMatchRule& Type(DBusMessageType messageType);

    // The name of the sender to match on
    DBusMatchRule& Sender(std::variant<DBusWellKnownName, DBusUniqueConnectionName> name);

    // The interface of the message to match on
    DBusMatchRule& Interface(DBusInterfaceName interface);

    // The name of the member to match on
    DBusMatchRule& Member(std::string member);

    // The ObjectPath of the message to match on
    DBusMatchRule& Path(ObjectPath path);

    // Matches messages for which the given ObjectPath is either the exact value. or that value followed by one or more
    // path components. See
    // https://dbus.freedesktop.org/doc/dbus-specification.html#:~:text=Matches%20messages%20which%20are%20sent%20from%20or%20to%20an%20object%20for%20which%20the%20object%20path%20is%20either%20the%20given%20value%2C%20or%20that%20value%20followed%20by%20one%20or%20more%20path%20components.
    // for more info
    DBusMatchRule& PathNamespace(ObjectPath path);

    // The Unique Connection name of the message's sender to match on
    DBusMatchRule& Destination(DBusUniqueConnectionName destination);

    // Match an argument of the STRING type by index and value
    DBusMatchRule& Argument(uint8_t index, std::string member);

    // Match an argument that represents a filesystem path.
    // See
    // https://dbus.freedesktop.org/doc/dbus-specification.html#:~:text=Argument%20path%20matches%20provide%20a%20specialised%20form%20of%20wildcard%20matching%20for%20path%2Dlike%20namespaces.
    // for more info
    DBusMatchRule& ArgumentPath(uint8_t index, std::string member);

    // Match a message whose first argument is of type STRING and a bus name / interface name.
    DBusMatchRule& ArgumentNamespace(std::variant<DBusWellKnownName, DBusUniqueConnectionName, std::string> name);

    // Deprecated DBus behaviour, preferably don't use this.
    DBusMatchRule& EavesDrop(bool eavesdrop);

    std::string GetRule() const;

    // Check if the incoming message matches any of our defined rules
    // We also pass a list of well-known names here in case the SENDER header field in the incoming message is a unique
    // connection name. That way, we can still check for well-known name instead of forcing the user to figure out the
    // unique connection name of the sender.
    bool Matches(IncomingDBusMessage const& message, std::vector<std::string> const& wellKnownNames) const;

    bool operator==(DBusMatchRule const&) const noexcept = default;
  };
}  // namespace cxxbus
