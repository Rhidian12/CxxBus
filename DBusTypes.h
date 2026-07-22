#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>

using byte = uint8_t;

enum class DBusMessageType : uint8_t
{
  INVALID = 0,
  METHOD_CALL = 1,    // May prompt a reply
  METHOD_RETURN = 2,  // Method reply with returned data
  ERROR = 3,          // Error reply. If first argument exists and is a string, it is an error message
  SIGNAL = 4,         // Signal emission

  OPTIONAL = 254,  // Internal Usage. Not part of DBus Spec.
  NONE = 255,      // Internal Usage. Not part of DBus Spec.
};

enum class Endianness : uint8_t
{
  LITTLE_ENDIAN_TYPE = 'l',
  BIG_ENDIAN_TYPE = 'B'
};

enum class DBusMessageFlags : uint8_t
{
  NONE = 0,
  NO_REPLY_EXPECTED = 0x1,               // We don't expect replies, even if it can have replies
  NO_AUTO_START = 0x2,                   // The bus must not launch an owner for the destination name in response to this message
  ALLOW_INTERACTIVE_AUTHORIZATION = 0x4  // We are informing the receiver of our message that we are prepared to wait for interactive authorization
};

enum class HeaderFieldCode : uint8_t
{
  INVALID = 0,
  PATH = 1,          // The object to send a call to, or the object a signal is emitted from. Controlled by message sender
  INTERFACE = 2,     // The interface to invoke a method call on, or the interface a signal is emitted from. Optional for method calls, required for signals.
                     // Controlled by message sender.
  MEMBER = 3,        // The member, either method name or signal name. Controlled by message sender
  ERROR_NAME = 4,    // The name of the error that occurred.
  REPLY_SERIAL = 5,  // The serial number of the message this message is a reply to. Controlled by the message sender.
  DESTINATION = 6,   // Name of the connection message is intended for. Controlled by the message sender.
  SENDER = 7,        // Unique name of the sending connection. On a message bus, controlled by the message bus, otherwise controlled by message sender.
  SIGNATURE = 8,     // Signature of the message body. If not present, assume signature is "" (meaning body must be 0-length). Controlled by message sender
  UNIX_FDS =
      9,  // Number of Unix file descriptors that accompany the message. If not present, assume no FDs accompany the message. Controlled by message sender.

  NONE = 255  // Internal Usage. Not part of DBus Spec.
};

enum class DBusTypeCodes : uint8_t
{
  BYTE = 'y',     // uint8_t
  BOOLEAN = 'b',  // boolean
  INT16 = 'n',
  UINT16 = 'q',
  INT32 = 'i',
  UINT32 = 'u',
  INT64 = 'x',
  UINT64 = 't',
  DOUBLE = 'd',
  UNIX_FD = 'h',  // uint32_t representing Unix FD

  STRING = 's',
  OBJECT_PATH = 'o',  // DBus Object Path
  SIGNATURE = 'g',    // Zero or more Single Complete Types

  ARRAY = 'a',         // Start of Array
  STRUCT = 'r',        // Not actually used in the DBus protocol. Used to represent the general concept of a struct
  STRUCT_BEGIN = '(',  // Beginning of a Struct definition
  STRUCT_END = ')',    // End of a struct definition
  VARIANT = 'v',       // Variant. Type of the value is part of the value itself
  DICT = 'e',          // Not actually used in the DBus protocol. Used to represent the general concept of a dict.
  DICT_BEGIN = '{',    // Beginning of a dict definition
  DICT_END = '}',      // End of a dict definition

  INVALID = 255  // Internal Usage. Not part of DBus Spec.
};

struct HeaderField
{
  HeaderFieldCode decimalCode;
  DBusTypeCodes type;  // dbus type
  std::array<DBusMessageType, 2> requiredMessageType;
};

class ObjectPath
{
 private:
  std::string m_path;

 public:
  ObjectPath() = default;
  ObjectPath(std::string path)
    : m_path(std::move(path))
  {
  }

  uint32_t size() const
  {
    return m_path.size();
  }

  explicit operator std::string() const
  {
    return m_path;
  }

  bool Empty() const;

  auto operator<=>(ObjectPath const&) const noexcept = default;
};

class Signature
{
 private:
  std::string m_signature;

 public:
  Signature(std::string signature);

  uint32_t Size() const;
  explicit operator std::string() const;
  std::string const& GetSignature() const;
  // Get the alignment of the contained signature
  uint8_t GetAlignmentOfSignature() const;
  bool Empty() const;

  auto operator<=>(Signature const&) const noexcept = default;
  bool operator==(Signature const&) const = default;
  bool operator==(std::string const& str) const;
};

template <typename... Ts>
class MultipleCompleteTypes
{
 private:
  std::tuple<Ts...> m_types;

 public:
  using type = std::tuple<Ts...>;

 public:
  template <typename... Us>
  MultipleCompleteTypes(Us&&... types)
    : m_types(std::forward<Us>(types)...)
  {
  }

  std::tuple<Ts...> const& GetTypes() const
  {
    return m_types;
  }

  template <size_t I>
  auto GetType() const
  {
    return std::get<I>(m_types);
  }
};

// Signature of a DBus Header is yyyyuua(yv)
// y = byte
// u = uint32_t
// a = array
// v = variant

// 1st byte is Endianness. ASCII 'l' for little-endian, 'B' for big-endian
// 2nd byte is message type
// 3rd byte is bitwise-OR flags
// 4th byte is major protocol version, is always 1
// 1st uint32_t is length in bytes of the message body, starting from the end of the header
// 2nd uint32_t is the serial of this message, used as a cookie by the sender to identify the reply correspending to this request.
// Must be non-zero value Array of struct of byte, variant are the header fields.
// The message type specifies which fields are required
// Here we keep track of the set starting size of any DBus message:
// The 4 bytes, the 2 u32's. We use this data to parse the DBus Message piece-by-piece
inline static uint32_t constexpr FIRST_HEADER_PART_SIZE = sizeof(uint8_t) * 4 + sizeof(uint32_t) * 2;

// Alignment boundary of the DBus Message Body (not the Header)
inline static uint8_t constexpr DBUS_MESSAGE_BODY_ALIGNMENT = 8;
