#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "DBus.h"

class DBusMessage
{
 private:
  std::string m_method;
  std::string m_path;
  std::string m_interface;
  std::vector<DBusMessageFlags> m_flags;

  std::string m_signature;
  std::vector<uint8_t> m_messageBody;

 private:
  DBusMessage(std::string method, std::string path, std::string interface, std::string signature, std::vector<byte> messageBody);

 public:
  DBusMessage() = default;
  DBusMessage(std::string method, std::string path, std::string interface);
  template <IsSerializableDBusType T>
  DBusMessage(T&& value, std::string method, std::string path, std::string interface)
    : DBusMessage(std::move(method), std::move(path), std::move(interface), GetTypeSignature<std::remove_cvref_t<T>>(), MarshalDBusType(std::forward<T>(value)))
  {
  }

  DBusMessage(DBusMessage const&) = default;
  DBusMessage(DBusMessage&&) = default;
  DBusMessage& operator=(DBusMessage const&) = default;
  DBusMessage& operator=(DBusMessage&&) = default;

  std::vector<uint8_t> Serialize(uint32_t serial) const;

  std::vector<DBusMessageFlags> const& GetFlags() const
  {
    return m_flags;
  }
};