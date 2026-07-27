#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "DBus.h"
#include "DBusHelpers.h"
#include "DBusTypes.h"

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
  std::optional<std::string> m_destination;
  std::vector<uint8_t> m_messageBody;

 public:
  DBusMessage() = default;

  // The methods to create a DBus Message with. It starts with `Create()` and then allows other methods to chain into it.
  static DBusMessage Create(std::string method);
  DBusMessage& Path(ObjectPath path);
  DBusMessage& Interface(std::string interface);
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

  ObjectPath const& GetPath() const;
  Signature const& GetSignature() const;
  std::string const& GetInterface() const;
  std::string const& GetDestination() const;
  std::string const& GetMember() const;

  // Only useful for debugging purposes
  std::vector<byte> const& GetRawData() const;
};
