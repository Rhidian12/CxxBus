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

#include "DBusTypes.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <ranges>

#include "DBusHelpers.h"

namespace cxxbus
{
  namespace
  {
    constexpr uint8_t MAX_DBUS_NAME_LENGTH = 255;

    // Returns std::nullopt if the provided name is valid
    // Returns a filled std::optional containing an error reason if the name is invalid
    std::optional<std::string> ValidateDBusName(std::string const& name, bool validateWellKnownName)
    {
      // A well-known name must be:
      //  - Non-empty
      //  - Not start with ':'
      //  - Not start with '.'
      //  - Composed of one or more elements separated by a '.'. All elements must be non-empty
      //  - Names must contain at least one '.' (and thus at least 2 elements)
      //  - Not be longer than 255

      std::string const prefix{validateWellKnownName ? "Well-known" : "Unique connection"};

      if (name.empty()) return std::format("{} name cannot be empty", prefix);
      if (validateWellKnownName && name[0] == ':') return "Well-known name cannot start with ':'";
      if (!validateWellKnownName && name[0] != ':') return "Unique connection name must start with ':'";
      if (name[0] == '.') return std::format("{} name cannot start with '.'", prefix);
      if (std::count(name.begin(), name.end(), '.') == 0) return std::format("{} must contain at least 1 '.'", prefix);
      if (name.size() >= MAX_DBUS_NAME_LENGTH)
        return std::format("{} name must be shorter than 255 characters", prefix);

      return std::nullopt;
    }

    // Returns std::nullopt if the provided name is valid
    // Returns a filled std::optional containing an error reason if the name is invalid
    std::optional<std::string> ValidateDBusInterfaceName(std::string const& name)
    {
      // An interface name must be:
      //  - Non-empty
      //  - Composed of one or more elements seperated by a '.'. All elements must be non-empty
      //  - Names must contain at least one '.' (and thus at least 2 elements)
      //  - Not be longer than 255

      if (name.empty()) return "Interface name cannot be empty";
      if (std::count(name.begin(), name.end(), '.') == 0) return "Interface name must contain at least 1 '.'";
      if (name.size() >= MAX_DBUS_NAME_LENGTH) return "Interface name must be shorter than 255 characters";

      return std::nullopt;
    }

    // Returns std::nullopt if the provided path is valid
    // Returns a filled std::optional containing an error reason if the path is invalid
    std::optional<std::string> ValidateDBusObjectPath(std::string const& path)
    {
      // An Object Path must be:
      //  - Non-empty
      //  - Start with '/'
      //  - Each element must only contain the ASCII characters '[A-Z][a-z][0-9]_'
      //  - No element can be empty
      //  - Multiple '/' cannot appear in sequence
      //  - A trailing '/' is not allowed unless the path is the root path ('/')

      if (path.empty()) return "Object Path cannot be empty";
      if (path[0] != '/') return "Object Path must start with '/'";

      // First element is empty if first character is '/'
      auto range = path | std::views::split('/') | std::views::drop(1);
      std::vector<std::string_view> const elements(range.begin(), range.end());

      if (path.size() > 1 && std::ranges::any_of(elements, [](std::string_view elem) { return elem.empty(); }))
      {
        return "Object Path cannot contain empty elements in between '/'";
      }

      if (std::ranges::any_of(elements,
                              [](std::string_view elem)
                              {
                                return std::ranges::any_of(elem,
                                                           [](unsigned char c)
                                                           {
                                                             return !((c >= 'A' && c <= 'Z') ||
                                                                      (c >= 'a' && c <= 'z') ||
                                                                      (c >= '0' && c <= '9') || c == '_');
                                                           });
                              }))
      {
        return "Object Path elements can only contain characters in the following range: '[A-Z][a-z][0-9]_'";
      }

      if (path.contains("//"))
      {
        return "Object Path cannot contain consecutive '/'";
      }

      if (path.back() == '/' && (path.size() > 1 || (path.size() == 1 && path[0] != '/')))
      {
        return "Object Path cannot end with '/' unless it is the root path ('/')";
      }

      return std::nullopt;
    }
  }  // namespace

  ObjectPath::ObjectPath(std::string path)
    : m_path()
  {
    if (auto ret{ValidateDBusObjectPath(path)}; ret.has_value())
    {
      throw InvalidDBusObjectPath{ret.value()};
    }

    m_path = std::move(path);
  }

  std::string const& ObjectPath::GetPath() const
  {
    return m_path;
  }

  bool ObjectPath::operator==(std::string const& str) const noexcept
  {
    return m_path == str;
  }

  Signature::Signature(std::string signature)
    : m_signature(std::move(signature))
  {
  }

  uint32_t Signature::Size() const
  {
    return m_signature.size();
  }

  Signature::operator std::string() const
  {
    return m_signature;
  }

  std::string const& Signature::GetSignature() const
  {
    return m_signature;
  }

  // Get the alignment of the contained signature
  uint8_t Signature::GetAlignmentOfSignature() const
  {
    return ::cxxbus::GetAlignmentOfSignature(*this);
  }

  bool Signature::Empty() const
  {
    return m_signature.empty();
  }

  bool Signature::operator==(std::string const& str) const
  {
    return m_signature == str;
  }

  bool ObjectPath::Empty() const
  {
    return m_path.empty();
  }

  DBusUniqueConnectionName::DBusUniqueConnectionName(std::string uniqueConnectionName)
    : m_name()
  {
    if (auto result{ValidateDBusName(uniqueConnectionName, false)}; result.has_value())
    {
      throw InvalidDBusName{result.value()};
    }

    m_name = std::move(uniqueConnectionName);
  }

  std::string const& DBusUniqueConnectionName::GetName() const
  {
    return m_name;
  }

  uint32_t DBusUniqueConnectionName::size() const
  {
    return m_name.size();
  }

  bool DBusUniqueConnectionName::empty() const
  {
    return m_name.empty();
  }

  DBusUniqueConnectionName::operator std::string() const
  {
    return m_name;
  }

  DBusWellKnownName::DBusWellKnownName(std::string wellKnownName)
    : m_name()
  {
    if (auto result{ValidateDBusName(wellKnownName, true)}; result.has_value())
    {
      throw InvalidDBusName{result.value()};
    }

    m_name = std::move(wellKnownName);
  }

  std::string const& DBusWellKnownName::GetName() const
  {
    return m_name;
  }

  uint32_t DBusWellKnownName::size() const
  {
    return m_name.size();
  }

  DBusWellKnownName::operator std::string() const
  {
    return m_name;
  }

  DBusInterfaceName::DBusInterfaceName(std::string interfaceName)
    : m_name()
  {
    if (auto result{ValidateDBusInterfaceName(interfaceName)}; result.has_value())
    {
      throw InvalidDBusName{result.value()};
    }

    m_name = std::move(interfaceName);
  }

  std::string const& DBusInterfaceName::GetName() const
  {
    return m_name;
  }

  uint32_t DBusInterfaceName::size() const
  {
    return m_name.size();
  }

  bool DBusInterfaceName::empty() const
  {
    return m_name.empty();
  }

  DBusInterfaceName::operator std::string() const
  {
    return m_name;
  }

  bool DBusInterfaceName::operator==(std::string const& str) const
  {
    return m_name == str;
  }
}  // namespace cxxbus
