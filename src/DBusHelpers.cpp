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

#include "DBusHelpers.h"

#include <algorithm>
#include <format>
#include <stdexcept>

#include "DBusTypes.h"
#include "IncomingDBusMessage.h"

namespace cxxbus
{
  bool IsDBusBasicFixedTypeCode(unsigned char c)
  {
    switch (static_cast<DBusTypeCodes>(c))
    {
      case DBusTypeCodes::BYTE:
      case DBusTypeCodes::BOOLEAN:
      case DBusTypeCodes::INT16:
      case DBusTypeCodes::UINT16:
      case DBusTypeCodes::INT32:
      case DBusTypeCodes::UINT32:
      case DBusTypeCodes::INT64:
      case DBusTypeCodes::UINT64:
      case DBusTypeCodes::DOUBLE:
      case DBusTypeCodes::UNIX_FD:
        return true;
      default:
        return false;
    }
  }

  bool IsDBusBasicStringlikeTypeCode(unsigned char c)
  {
    switch (static_cast<DBusTypeCodes>(c))
    {
      case DBusTypeCodes::STRING:
      case DBusTypeCodes::OBJECT_PATH:
      case DBusTypeCodes::SIGNATURE:
        return true;
      default:
        return false;
    }
  }

  bool IsDBusBasicTypeCode(unsigned char c)
  {
    return IsDBusBasicFixedTypeCode(c) || IsDBusBasicStringlikeTypeCode(c);
  }

  bool IsDBusContainerTypeCode(unsigned char c)
  {
    switch (static_cast<DBusTypeCodes>(c))
    {
      case DBusTypeCodes::ARRAY:
      case DBusTypeCodes::STRUCT_BEGIN:
      case DBusTypeCodes::STRUCT_END:
      case DBusTypeCodes::DICT_BEGIN:
      case DBusTypeCodes::DICT_END:
      case DBusTypeCodes::VARIANT:
        return true;
      default:
        return false;
    }
  }

  bool IsDBusTypeCode(unsigned char c)
  {
    return IsDBusBasicTypeCode(c) || IsDBusContainerTypeCode(c);
  }

  bool IsDBusTypeCode(std::string const& str)
  {
    for (unsigned char c : str)
    {
      if (!IsDBusTypeCode(c))
      {
        return false;
      }
    }

    return true;
  }

  bool AreDBusTypeCodeBracketsEven(std::string const& str)
  {
    int32_t counter{};

    for (unsigned char c : str)
    {
      switch (static_cast<DBusTypeCodes>(c))
      {
        case DBusTypeCodes::DICT_BEGIN:
        case DBusTypeCodes::STRUCT_BEGIN:
          ++counter;
          break;
        case DBusTypeCodes::STRUCT_END:
        case DBusTypeCodes::DICT_END:
          --counter;
          break;
        default:
          break;
      }
    }

    return counter == 0;
  }

  uint8_t GetAlignmentOfSignature(Signature const& signature)
  {
    switch (static_cast<DBusTypeCodes>(signature.GetSignature()[0]))
    {
      case DBusTypeCodes::BYTE:
      case DBusTypeCodes::SIGNATURE:
      case DBusTypeCodes::VARIANT:  // Alignment of Signature
        return 1;
      case DBusTypeCodes::INT16:
      case DBusTypeCodes::UINT16:
        return 2;
      case DBusTypeCodes::BOOLEAN:
      case DBusTypeCodes::UNIX_FD:
      case DBusTypeCodes::INT32:
      case DBusTypeCodes::UINT32:
      case DBusTypeCodes::STRING:
      case DBusTypeCodes::OBJECT_PATH:
      case DBusTypeCodes::ARRAY:
        return 4;
      case DBusTypeCodes::INT64:
      case DBusTypeCodes::UINT64:
      case DBusTypeCodes::DOUBLE:
      case DBusTypeCodes::STRUCT_BEGIN:
        return 8;
      default:
        throw std::runtime_error{
            std::format("Alignment of signature '{}' cannot be requested", signature.GetSignature()[0])};
    }
  }

  std::string ParseDBusAddress(BusType type)
  {
    char const* rawAddress = getenv(type == BusType::SESSION ? "DBUS_SESSION_BUS_ADDRESS" : "DBUS_SYSTEM_BUS_ADDRESS");
    if (!rawAddress)
    {
      return "";
    }

    // Looks something like: unix:path=/run/user/1000/bus or unix:path=/var/run/dbus/system_bus_socket
    // after the 'unix:' prefix, we have multiple key=value pairs, separated by commas. We only care about the 'path' key
    std::string_view dbusAddress{rawAddress};

    if (!dbusAddress.starts_with("unix:"))
    {
      throw std::runtime_error{"Only support unix sockets for DBus-daemon connections"};
    }

    dbusAddress.remove_prefix(5);  // remove "unix:"

    std::size_t const pos = dbusAddress.find("path=");
    if (pos == std::string_view::npos)
    {
      throw std::runtime_error{"DBus-daemon address does not contain a 'path' key"};
    }

    std::size_t const endPos = dbusAddress.find(",", pos);

    return std::string{dbusAddress.substr(pos + 5, endPos - (pos + 5))};
  }

  std::string HexEncodeString(std::string const& str)
  {
    std::string newStr;
    std::ranges::for_each(str, [&newStr](unsigned char c) { newStr += std::format("{:x}", c); });
    return newStr;
  }

  boost::asio::awaitable<void> InvokeAsyncCallback(
      std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback, IncomingDBusMessage message)
  {
    co_return co_await callback(std::move(message));
  }
}  // namespace cxxbus
