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

#include <concepts>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "DBus.h"
#include "DBusHelpers.h"
#include "DBusTypes.h"
#include "IncomingDBusMessage.h"

namespace cxxbus
{
  class InvalidDBusPath : public std::runtime_error
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
    uint8_t m_flags{};
    DBusMessageType m_messageType;

    std::optional<Signature> m_signature;
    std::optional<std::string> m_destination;
    std::optional<std::string> m_errorName;
    std::optional<uint32_t> m_replySerial;
    std::vector<uint8_t> m_messageBody;

   public:
    DBusMessage() = default;

    template <typename TMethod, typename TPath>
      requires(std::constructible_from<std::string, TMethod> && std::same_as<std::remove_cvref_t<TPath>, ObjectPath>)
    static DBusMessage Method(TMethod&& method, TPath&& path)
    {
      DBusMessage message;
      message.m_method = std::forward<TMethod>(method);
      message.m_path = std::forward<TPath>(path);
      message.m_messageType = DBusMessageType::METHOD_CALL;
      return message;
    }

    template <typename TSignal, typename TPath, typename TInterface>
      requires(std::constructible_from<std::string, TSignal> && std::same_as<std::remove_cvref_t<TPath>, ObjectPath> &&
               std::same_as<std::remove_cvref_t<TInterface>, DBusInterfaceName>)
    static DBusMessage Signal(TSignal&& signal, TPath&& path, TInterface&& interface)
    {
      DBusMessage message;
      message.m_method = std::forward<TSignal>(signal);
      message.m_path = std::forward<TPath>(path);
      message.m_interface = std::forward<TInterface>(interface);
      message.m_messageType = DBusMessageType::SIGNAL;
      return message;
    }

    // [TODO]: Error names have the same nqaming requirements as DBus Interfaces
    template <typename TErrorName, typename TErrorMessage>
      requires(std::constructible_from<std::string, TErrorName> && std::constructible_from<std::string, TErrorMessage>)
    static DBusMessage Error(IncomingDBusMessage const& incomingMessage, TErrorName&& errorName,
                             TErrorMessage&& errorMessage)
    {
      DBusMessage message;
      message.m_messageType = DBusMessageType::ERROR;
      message.m_errorName = std::forward<TErrorName>(errorName);
      message.m_replySerial = incomingMessage.GetSerial();
      message.m_destination = incomingMessage.GetSender();
      message.Parameter(std::forward<TErrorMessage>(errorMessage));

      return message;
    }
    static DBusMessage Reply(IncomingDBusMessage const& incomingMessage);

    DBusMessage& Path(ObjectPath&& path);
    DBusMessage& Path(ObjectPath const& path);
    DBusMessage& Interface(DBusInterfaceName&& interface);
    DBusMessage& Interface(DBusInterfaceName const& interface);
    DBusMessage& Destination(std::string&& destination);
    DBusMessage& Destination(std::string const& destination);
    DBusMessage& Flag(DBusMessageFlags flag);
    template <typename T>
    DBusMessage& Parameter(T&& value)
    {
      m_signature = std::string{GetTypeSignature<std::remove_cvref_t<T>>()};
      m_messageBody = MarshalDBusType<T>(std::forward<T>(value));

      return *this;
    }

    DBusMessage(DBusMessage const&) = default;
    DBusMessage(DBusMessage&&) = default;
    DBusMessage& operator=(DBusMessage const&) = default;
    DBusMessage& operator=(DBusMessage&&) = default;

    std::vector<uint8_t> Serialize(uint32_t serial) const;

    uint8_t GetFlags() const;

    bool ExpectsReply() const;

    std::optional<ObjectPath> const& GetPath() const;
    std::optional<Signature> const& GetSignature() const;
    std::optional<DBusInterfaceName> const& GetInterface() const;
    std::optional<std::string> const& GetDestination() const;
    std::optional<std::string> const& GetMember() const;

    // Only useful for debugging purposes
    std::vector<byte> const& GetRawData() const;

    std::string GetInfo() const;
  };
}  // namespace cxxbus
