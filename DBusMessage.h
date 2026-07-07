#pragma once

#include "DBus.h"

#include <cstdint>
#include <string>
#include <vector>

class DBusMessage {
private:
  std::string m_method;
  std::string m_path;
  std::string m_interface;
  std::vector<DBusMessageFlags> m_flags;

  std::vector<uint8_t> m_messageBody;

public:
  DBusMessage(std::string const &method, std::string const &path,
              std::string const &interface)
      : m_method(method), m_path(path), m_interface(interface) {}

  // Adds a value to the message body
  template <IsDBusType T> void AddParameter(T &&value);

  std::vector<uint8_t> Serialize(uint32_t serial) const;

  std::vector<DBusMessageFlags> const &GetFlags() const { return m_flags; }
};

template <IsDBusType T> void DBusMessage::AddParameter(T &&value) {
  uint32_t size{};
  GetSizeOfDBusType(value, size);
  m_messageBody.reserve(m_messageBody.capacity() + size);
  m_messageBody.append_range(MarshalDBusType(std::forward<T>(value)));
}