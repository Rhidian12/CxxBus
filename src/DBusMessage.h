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
#include <stdexcept>
#include <string>
#include <vector>

#include "DBus.h"
#include "DBusHelpers.h"
#include "DBusTypes.h"

namespace cxxbus
{
  class IncomingDBusMessage;

  class InvalidDBusPath : public std::runtime_error
  {
   public:
    using std::runtime_error::runtime_error;
  };

  class DBusSerializationError : public std::runtime_error
  {
   public:
    using std::runtime_error::runtime_error;
  };

  class DBusMessage
  {
   private:
    std::optional<std::string> m_method;
    std::optional<ObjectPath> m_path;
    std::optional<DBusInterfaceName> m_interface;
    std::vector<DBusMessageFlags> m_flags;
    DBusMessageType m_messageType;

    std::optional<Signature> m_signature;
    std::optional<std::string> m_destination;
    std::optional<std::string> m_errorName;
    std::optional<uint32_t> m_replySerial;
    std::vector<uint8_t> m_messageBody;

   public:
    DBusMessage() = default;

    static DBusMessage Method(std::string method);
    static DBusMessage Reply(IncomingDBusMessage const& incomingMessage);
    static DBusMessage Signal(std::string signal);
    static DBusMessage Error(IncomingDBusMessage const& incomingMessage, std::string errorName,
                             std::string errorMessage);

    DBusMessage& Path(ObjectPath path);
    DBusMessage& Interface(DBusInterfaceName interface);
    DBusMessage& Destination(std::string destination);
    DBusMessage& Flag(DBusMessageFlags flag);
    template <typename T>
    DBusMessage& Parameter(T&& value)
    {
      m_signature = GetTypeSignature<std::remove_cvref_t<T>>();
      m_messageBody = MarshalDBusType<T>(std::forward<T>(value));

      return *this;
    }

    DBusMessage(DBusMessage const&) = default;
    DBusMessage(DBusMessage&&) = default;
    DBusMessage& operator=(DBusMessage const&) = default;
    DBusMessage& operator=(DBusMessage&&) = default;

    std::vector<uint8_t> Serialize(uint32_t serial) const;

    std::vector<DBusMessageFlags> const& GetFlags() const;

    std::optional<ObjectPath> const& GetPath() const;
    std::optional<Signature> const& GetSignature() const;
    std::optional<DBusInterfaceName> const& GetInterface() const;
    std::optional<std::string> const& GetDestination() const;
    std::optional<std::string> const& GetMember() const;

    // Only useful for debugging purposes
    std::vector<byte> const& GetRawData() const;
  };
}  // namespace cxxbus
