#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "DBus.h"
#include "DBusReply.h"

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
  std::string m_method;  // Member. Non-optional
  ObjectPath m_path;     // Non-optional
  std::optional<std::string> m_interface;
  std::vector<DBusMessageFlags> m_flags;

  std::optional<Signature> m_signature;
  std::vector<uint8_t> m_messageBody;

  DBusMessageHeader m_header;

 private:
  DBusMessage(std::string method, ObjectPath path, std::optional<std::string> interface, std::optional<Signature> signature, std::vector<byte> messageBody);

 public:
  DBusMessage() = default;
  DBusMessage(std::string method, ObjectPath path);
  DBusMessage(std::string method, ObjectPath path, std::string interface);
  template <IsDBusType T>
  DBusMessage(T&& value, std::string method, ObjectPath path, std::string interface)
    : DBusMessage(std::move(method), std::move(path), std::move(interface), GetTypeSignature<std::remove_cvref_t<T>>(), MarshalDBusType(std::forward<T>(value)))
  {
  }

  // For incoming messages that are NOT replies to an outgoing message
  DBusMessage(DBusMessageHeader header, std::vector<byte> messageBody);

  DBusMessage(DBusMessage const&) = default;
  DBusMessage(DBusMessage&&) = default;
  DBusMessage& operator=(DBusMessage const&) = default;
  DBusMessage& operator=(DBusMessage&&) = default;

  std::vector<uint8_t> Serialize(uint32_t serial) const;

  std::vector<DBusMessageFlags> const& GetFlags() const;

  DBusMessageHeader const& GetHeader() const;

  // Only useful for debugging purposes
  std::vector<byte> const& GetRawData() const;
};