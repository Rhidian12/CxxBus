#include "DBusMessage.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "DBus.h"
#include "DBusTypes.h"
#include "IncomingDBusMessage.h"

namespace cxxbus
{
  namespace
  {
    constexpr uint32_t MESSAGE_HEADER_PADDING = 8;

    template <DBusMessageType T>
    concept IsAcceptedMessageType = requires { requires T == DBusMessageType::METHOD_CALL || T == DBusMessageType::SIGNAL; };

    std::vector<byte> CreateDBusMessage(DBusMessageType msgType, uint32_t serial, std::vector<byte> messageBody,
                                        std::vector<DBusMessageFlags> const& messageFlags, std::optional<std::string> const& method,
                                        std::optional<ObjectPath> const& objectPath, std::optional<std::string> const& interface,
                                        std::optional<std::string> destination, std::optional<Signature> const& signature,
                                        std::optional<std::string> const& errorName, std::optional<uint32_t> const& replySerial)
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
      std::vector<HeaderField> requiredHeaderFields{std::ranges::to<std::vector>(std::views::filter(
          HEADER_FIELDS, [msgType](HeaderField const& headerField) { return std::ranges::contains(headerField.requiredMessageType, msgType); }))};
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
          throw std::runtime_error{"Not implemented yet"};
          break;
          case HeaderFieldCode::SENDER:
          case HeaderFieldCode::DESTINATION:
          case HeaderFieldCode::UNIX_FDS:
            throw std::runtime_error{"Sender, Destination and UNIX_FDS can never be required fields!"};
            break;
          case HeaderFieldCode::REPLY_SERIAL:
            if (!replySerial.has_value())
            {
              // [TODO]: Print this as a string
              throw DBusSerializationError{std::format("replySerial is required for message type {}", static_cast<uint8_t>(msgType))};
            }
            variant = Variant{replySerial.value()};
            break;
          case HeaderFieldCode::ERROR_NAME:
            if (!errorName.has_value() || errorName->empty())
            {
              // [TODO]: Print this as a string
              throw DBusSerializationError{std::format("ErrorName is required for message type {}", static_cast<uint8_t>(msgType))};
            }
            variant = Variant{errorName.value()};
            break;
          case HeaderFieldCode::PATH:
            if (!objectPath.has_value() || objectPath->Empty())
            {
              // [TODO]: Print this as a string
              throw DBusSerializationError{std::format("Path is required for message type {}", static_cast<uint8_t>(msgType))};
            }
            variant = Variant{*objectPath};
            break;
          case HeaderFieldCode::INTERFACE:
            if (!interface.has_value() || interface->empty())
            {
              // [TODO]: Print this as a string
              throw DBusSerializationError{std::format("Interface is required for message type {}", static_cast<uint8_t>(msgType))};
            }
            variant = Variant{interface.value()};
            break;
          case HeaderFieldCode::MEMBER:
            if (!method.has_value() || method->empty())
            {
              // [TODO]: Print this as a string
              throw DBusSerializationError{std::format("Method is required for message type {}", static_cast<uint8_t>(msgType))};
            }
            variant = Variant{*method};
            break;
          case HeaderFieldCode::SIGNATURE:
            if (!signature.has_value() || signature->Empty())
            {
              // [TODO]: Print this as a string
              throw DBusSerializationError{std::format("Signature is required for message type {} with non-empty body", static_cast<uint8_t>(msgType))};
            }
            variant = Variant{*signature};
            break;
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

      if (destination.has_value() && !destination->empty())
      {
        headerFields.push_back(std::make_tuple(static_cast<uint8_t>(HeaderFieldCode::DESTINATION), Variant{destination.value()}));
      }

      std::ranges::sort(headerFields, [](auto const& a, auto const& b) { return std::get<0>(a) < std::get<0>(b); });

      MultipleCompleteTypes<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t, std::vector<std::tuple<uint8_t, Variant>>> header{
          static_cast<uint8_t>(Endianness::LITTLE_ENDIAN_TYPE),                                                                        // Endianness
          static_cast<uint8_t>(msgType),                                                                                               // Message Type
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

  DBusMessage DBusMessage::Method(std::string method)
  {
    DBusMessage message;
    message.m_method = std::move(method);
    message.m_messageType = DBusMessageType::METHOD_CALL;
    return message;
  }

  DBusMessage DBusMessage::Reply(IncomingDBusMessage const & incomingMessage)
  {
    DBusMessage message;
    message.m_messageType = DBusMessageType::METHOD_RETURN;
    message.m_replySerial = incomingMessage.GetHeader().GetSerial();
    message.m_destination = incomingMessage.GetHeader().GetSender();
    return message;
  }

  DBusMessage DBusMessage::Signal(std::string signal)
  {
    DBusMessage message;
    message.m_method = std::move(signal);
    message.m_messageType = DBusMessageType::SIGNAL;
    return message;
  }

  DBusMessage DBusMessage::Error(IncomingDBusMessage const & incomingMessage, std::string errorName, std::string errorMessage)
  {
    DBusMessage message;
    message.m_messageType = DBusMessageType::ERROR;
    message.m_errorName = std::move(errorName);
    message.m_replySerial = incomingMessage.GetHeader().GetSerial();
    message.m_destination = incomingMessage.GetHeader().GetSender();
    message.Parameter(std::move(errorMessage));

    return message;
  }

  DBusMessage& DBusMessage::Path(ObjectPath path)
  {
    m_path = std::move(path);
    return *this;
  }

  DBusMessage& DBusMessage::Interface(std::string interface)
  {
    m_interface = std::move(interface);
    return *this;
  }

  DBusMessage& DBusMessage::Destination(std::string destination)
  {
    m_destination = std::move(destination);
    return *this;
  }

  DBusMessage& DBusMessage::Flag(DBusMessageFlags flag)
  {
    m_flags.push_back(flag);
    return *this;
  }

  std::vector<uint8_t> DBusMessage::Serialize(uint32_t serial) const
  {
    return CreateDBusMessage(m_messageType, serial, m_messageBody, m_flags, m_method, m_path, m_interface, m_destination, m_signature, m_errorName, m_replySerial);
  }

  std::vector<DBusMessageFlags> const& DBusMessage::GetFlags() const
  {
    return m_flags;
  }

  std::optional<ObjectPath> const& DBusMessage::GetPath() const
  {
    return m_path;
  }

  std::optional<Signature> const& DBusMessage::GetSignature() const
  {
    return m_signature;
  }

  std::optional<std::string> const& DBusMessage::GetInterface() const
  {
    return m_interface;
  }

  std::optional<std::string> const& DBusMessage::GetDestination() const
  {
    return m_destination;
  }

  std::optional<std::string> const& DBusMessage::GetMember() const
  {
    return m_method;
  }

  std::vector<byte> const& DBusMessage::GetRawData() const
  {
    return m_messageBody;
  }
}  // namespace cxxbus