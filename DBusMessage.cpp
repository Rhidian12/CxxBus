#include "DBusMessage.h"
#include "DBus.h"
#include <cstdint>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {
constexpr uint32_t MESSAGE_HEADER_PADDING = 8;

template <DBusMessageType T>
concept IsAcceptedMessageType = requires {
  requires T == DBusMessageType::METHOD_CALL || T == DBusMessageType::SIGNAL;
};

template <DBusMessageType MsgType>
std::vector<byte>
CreateDBusMessage(uint32_t serial, std::vector<byte> messageBody,
                  std::vector<DBusMessageFlags> const &messageFlags,
                  std::string const &method, std::string const &objectPath,
                  std::string const &interface, std::string const &error) {
  // Signature of a DBus Header is yyyyuua(yv)
  // y = byte
  // u = uint32_t
  // a = array
  // v = variant

  // 1st byte is Endianness. ASCII 'l' for little-endian, 'B' for big-endian
  // 2nd byte is message type
  // 3rd byte is bitwise-OR flags
  // 4th byte is major protocol version, is always 1
  // 1st uint32_t is length in bytes of the message body, starting from the end
  // of the header 2nd uint32_t is the serial of this message, used as a cookie
  // by the sender to identify the reply correspending to this request. Must be
  // non-zero value Array of struct of byte, variant are the header fields. The
  // message type specifies which fields are required

  std::vector<byte> dbusMessage{};
  // Do a bit of reserving for the header of our message
  dbusMessage.reserve(4 * sizeof(uint8_t) + 2 * sizeof(uint32_t));

  dbusMessage.append_range(
      MarshalDBusType(static_cast<uint8_t>(Endianness::LITTLE_ENDIAN_TYPE)));

  dbusMessage.append_range(MarshalDBusType(static_cast<uint8_t>(MsgType)));

  // Same as the old std::accumulate
  std::vector<uint8_t> messageFlagsCasted;
  std::ranges::transform(messageFlags, std::back_inserter(messageFlagsCasted),
                         [](DBusMessageFlags flag) -> uint8_t {
                           return static_cast<uint8_t>(flag);
                         });
  dbusMessage.append_range(MarshalDBusType(std::ranges::fold_left(
      messageFlagsCasted, static_cast<uint8_t>(0),
      [](uint8_t a, uint8_t b) -> uint8_t { return a | b; })));

  dbusMessage.append_range(MarshalDBusType(1)); // Major Version

  dbusMessage.append_range(MarshalDBusType(static_cast<uint32_t>(
      messageBody.size()))); // Length of the message body in bytes

  dbusMessage.append_range(MarshalDBusType(serial)); // serial

  for (HeaderField const &headerField : HEADER_FIELDS) {
    if (!std::ranges::contains(headerField.requiredMessageType, MsgType)) {
      continue;
    }

    std::optional<Variant> variant{std::nullopt};
    switch (headerField.decimalCode) {
    case HeaderFieldCode::NONE:
    case HeaderFieldCode::INVALID:
    case HeaderFieldCode::REPLY_SERIAL:
    case HeaderFieldCode::ERROR_NAME:
      std::unreachable();
      break;
    case HeaderFieldCode::PATH:
      variant = Variant{objectPath};
      break;
    case HeaderFieldCode::INTERFACE:
      variant = Variant{interface};
      break;
    case HeaderFieldCode::MEMBER:
      variant = Variant{method};
      break;
    // [TODO]: Implement all header field codes
    case HeaderFieldCode::DESTINATION:
    case HeaderFieldCode::SENDER:
    case HeaderFieldCode::SIGNATURE:
    case HeaderFieldCode::UNIX_FDS:
      throw std::runtime_error{"Not implemented yet"};
    }
    
    dbusMessage.append_range(MarshalDBusType(std::make_tuple(
        static_cast<uint8_t>(headerField.decimalCode), *variant)));
  }

  ApplyPadding(dbusMessage, MESSAGE_HEADER_PADDING);

  dbusMessage.append_range(messageBody);

  return dbusMessage;
}
} // namespace

std::vector<uint8_t> DBusMessage::Serialize(uint32_t serial) const {
  return CreateDBusMessage<DBusMessageType::METHOD_CALL>(
      serial, m_messageBody, m_flags, m_method, m_path, m_interface, "");
}