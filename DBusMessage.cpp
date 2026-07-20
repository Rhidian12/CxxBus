#include "DBusMessage.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>

#include "DBus.h"
#include "DBusTypes.h"

namespace
{
  constexpr uint32_t MESSAGE_HEADER_PADDING = 8;

  template <DBusMessageType T>
  concept IsAcceptedMessageType = requires { requires T == DBusMessageType::METHOD_CALL || T == DBusMessageType::SIGNAL; };

  template <DBusMessageType MsgType>
  std::vector<byte> CreateDBusMessage(uint32_t serial, std::vector<byte> messageBody, std::vector<DBusMessageFlags> const& messageFlags,
                                      std::string const& method, ObjectPath const& objectPath, std::optional<std::string> const& interface,
                                      std::optional<Signature> const& signature, std::string const& error)
  {
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

    // Same as the old std::accumulate
    std::vector<uint8_t> messageFlagsCasted;
    std::ranges::transform(messageFlags, std::back_inserter(messageFlagsCasted), [](DBusMessageFlags flag) -> uint8_t { return static_cast<uint8_t>(flag); });

    std::vector<std::tuple<uint8_t, Variant>> headerFields{};
    std::vector<HeaderField> requiredHeaderFields{std::ranges::to<std::vector>(
        std::views::filter(HEADER_FIELDS, [](HeaderField const& headerField) { return std::ranges::contains(headerField.requiredMessageType, MsgType); }))};
    if (!messageBody.empty())
    {
      requiredHeaderFields.push_back(
          *std::ranges::find_if(HEADER_FIELDS, [](HeaderField const& headerField) { return headerField.decimalCode == HeaderFieldCode::SIGNATURE; }));
    }

    for (HeaderField const& headerField : requiredHeaderFields)
    {
      std::optional<Variant> variant{std::nullopt};
      switch (headerField.decimalCode)
      {
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
          if (!interface.has_value() || interface->empty())
          {
            // Print this as a string
            throw DBusSerializationError{std::format("Interface is required for message type {}", static_cast<uint8_t>(MsgType))};
          }
          variant = Variant{interface.value()};
          break;
        case HeaderFieldCode::MEMBER:
          variant = Variant{method};
          break;
        case HeaderFieldCode::SIGNATURE:
          if (!signature.has_value() || signature->Empty())
          {
            throw DBusSerializationError{std::format("Signature is required for message type {} with non-empty body", static_cast<uint8_t>(MsgType))};
          }
          variant = Variant{*signature};
          break;
        // [TODO]: Implement all header field codes
        case HeaderFieldCode::DESTINATION:
        case HeaderFieldCode::SENDER:
        case HeaderFieldCode::UNIX_FDS:
          throw std::runtime_error{"Not implemented yet"};
      }

      headerFields.push_back(std::make_tuple(static_cast<uint8_t>(headerField.decimalCode), *variant));
    }

    // Interface is often optional, but if provided, use it
    if (interface.has_value() &&
        std::ranges::find_if(headerFields, [](std::tuple<uint8_t, Variant> const& field)
                             { return std::get<0>(field) == static_cast<uint8_t>(HeaderFieldCode::INTERFACE); }) == headerFields.cend())
    {
      headerFields.push_back(std::make_tuple(static_cast<uint8_t>(HeaderFieldCode::INTERFACE), Variant{interface.value()}));
    }

    std::ranges::sort(headerFields, [](auto const& a, auto const& b) { return std::get<0>(a) < std::get<0>(b); });

    MultipleCompleteTypes<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t, std::vector<std::tuple<uint8_t, Variant>>> header{
        static_cast<uint8_t>(Endianness::LITTLE_ENDIAN_TYPE),                                                                        // Endianness
        static_cast<uint8_t>(MsgType),                                                                                               // Message Type
        std::ranges::fold_left(messageFlagsCasted, static_cast<uint8_t>(0), [](uint8_t a, uint8_t b) -> uint8_t { return a | b; }),  // Flags
        static_cast<uint8_t>(1),                                                                                                     // Major version
        static_cast<uint32_t>(messageBody.size()),  // Length of the message body in bytes
        serial,                                     // Serial as u32
        headerFields                                // Our array of header fields
    };

    dbusMessage.append_range(MarshalDBusType(header));
    ApplyPadding(dbusMessage, MESSAGE_HEADER_PADDING);

    dbusMessage.append_range(messageBody);

    return dbusMessage;
  }
}  // namespace

DBusMessage::DBusMessage(std::string method, ObjectPath path, std::string interface)
  : DBusMessage(std::move(method), std::move(path), std::move(interface), std::nullopt, {})
{
}

DBusMessage::DBusMessage(std::string method, ObjectPath path, std::optional<std::string> interface, std::optional<Signature> signature,
                         std::vector<byte> messageBody)
  : m_method(std::move(method))
  , m_path(std::move(path))
  , m_interface(std::move(interface))
  , m_signature(std::move(signature))
  , m_flags()
  , m_messageBody(std::move(messageBody))
{
  if (m_path.Empty())
  {
    throw InvalidDBusPath{"Object Path cannot be empty when constructing DBus Message"};
  }
}

DBusMessage::DBusMessage(DBusMessageHeader header, std::vector<byte> messageBody)
  : m_header(std::move(header))
  , m_messageBody(std::move(messageBody))
{
}

std::vector<uint8_t> DBusMessage::Serialize(uint32_t serial) const
{
  return CreateDBusMessage<DBusMessageType::METHOD_CALL>(serial, m_messageBody, m_flags, m_method, m_path, m_interface, m_signature, "");
}

std::vector<DBusMessageFlags> const& DBusMessage::GetFlags() const
{
  return m_flags;
}

DBusMessageHeader const& DBusMessage::GetHeader() const 
{
  return m_header;
}

std::vector<byte> const& DBusMessage::GetRawData() const
{
  return m_messageBody;
}