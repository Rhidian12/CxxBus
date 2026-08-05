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

#include "IncomingDBusMessage.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "DBus.h"
#include "DBusTypes.h"
#include "magic_enum.hpp"

namespace cxxbus
{
  namespace
  {
    DBusMessageHeader::ReplyData UnmarshalDBusHeader(std::vector<byte> dbusMessage)
    {
      // In this function we parse everything up until the array of variants, BUT INCLUDING the length of the array of
      // variants That way, we know how many bytes to read in always (16 at first, followed by the length of the header
      // fields, followed by padding + length of message)

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
      // 2nd uint32_t is the serial of this message, used as a cookie by the sender to identify the reply correspending
      // to this request. Must be non-zero value Array of struct of byte, variant are the header fields. The message
      // type specifies which fields are required

      if (dbusMessage.size() != FIRST_HEADER_PART_SIZE)
      {
        throw DBusMalformedInputError{std::format("Incoming DBus header should be {} bytes, it is {} bytes instead",
                                                  FIRST_HEADER_PART_SIZE, dbusMessage.size())};
      }

      auto header = UnmarshalDBusType<MultipleCompleteTypes<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t>>(
          dbusMessage, "yyyyuu");

      return {.serial = header.GetType<5>(),
              .replySerial = 0,
              .messageType = static_cast<DBusMessageType>(header.GetType<1>()),
              .objectPath = {},
              .interface = {},
              .member = {},
              .errorName = std::nullopt,
              .signature = std::nullopt,
              .sender = std::nullopt,
              .destination = std::nullopt,
              .messageLength = header.GetType<4>(),
              .headerFieldLength = 0,
              .headerFields = {}};
    }

    void UnmarshalDBusHeader(std::vector<byte> const& dbusMessage, DBusMessageHeader::ReplyData& data,
                             uint32_t& arrPointer)
    {
      auto headerFields =
          UnmarshalDBusType<std::vector<std::tuple<uint8_t, Variant>>>(dbusMessage, "a(yv)", arrPointer);

      std::vector<DBusMessageHeader::HeaderFieldReplyData> headerFieldData{};
      std::ranges::transform(headerFields, std::back_inserter(headerFieldData),
                             [](std::tuple<uint8_t, Variant> const& headerData)
                             {
                               return DBusMessageHeader::HeaderFieldReplyData{
                                   .code = static_cast<HeaderFieldCode>(std::get<0>(headerData)),
                                   .data = std::get<1>(headerData)};
                             });

      auto const signatureIt =
          std::ranges::find_if(headerFieldData, [](DBusMessageHeader::HeaderFieldReplyData const& data)
                               { return data.code == HeaderFieldCode::SIGNATURE; });
      if (signatureIt == headerFieldData.cend())
      {
        // No signature provided, so this means our message MUST be empty
        if (data.messageLength != 0)
        {
          throw DBusMalformedInputError{
              "Incoming DBus message did not specify a signature while providing a non-zero body."};
        }
      }

      auto const serialIt =
          std::ranges::find_if(headerFieldData, [](DBusMessageHeader::HeaderFieldReplyData const& data)
                               { return data.code == HeaderFieldCode::REPLY_SERIAL; });
      if (serialIt == headerFieldData.cend())
      {
        auto const requiredHeaderFieldIt = std::ranges::find_if(
            HEADER_FIELDS, [](HeaderField const& field) { return field.decimalCode == HeaderFieldCode::REPLY_SERIAL; });
        assert(requiredHeaderFieldIt != std::ranges::end(HEADER_FIELDS));
        if (std::ranges::contains(requiredHeaderFieldIt->requiredMessageType, data.messageType))
        {
          throw DBusMalformedInputError{"Incoming DBus message is missing the required 'REPLY_SERIAL' header field"};
        }
      }

      auto const objectPathIt =
          std::ranges::find_if(headerFieldData, [](DBusMessageHeader::HeaderFieldReplyData const& data)
                               { return data.code == HeaderFieldCode::PATH; });
      if (objectPathIt == headerFieldData.cend())
      {
        auto const requiredHeaderFieldIt = std::ranges::find_if(
            HEADER_FIELDS, [](HeaderField const& field) { return field.decimalCode == HeaderFieldCode::PATH; });
        assert(requiredHeaderFieldIt != std::ranges::end(HEADER_FIELDS));
        if (std::ranges::contains(requiredHeaderFieldIt->requiredMessageType, data.messageType))
        {
          throw DBusMalformedInputError{"Incoming DBus message is missing the required 'PATH' header field"};
        }
      }

      auto const interfaceIt =
          std::ranges::find_if(headerFieldData, [](DBusMessageHeader::HeaderFieldReplyData const& data)
                               { return data.code == HeaderFieldCode::INTERFACE; });
      if (interfaceIt == headerFieldData.cend())
      {
        auto const requiredHeaderFieldIt = std::ranges::find_if(
            HEADER_FIELDS, [](HeaderField const& field) { return field.decimalCode == HeaderFieldCode::INTERFACE; });
        assert(requiredHeaderFieldIt != std::ranges::end(HEADER_FIELDS));
        if (std::ranges::contains(requiredHeaderFieldIt->requiredMessageType, data.messageType))
        {
          throw DBusMalformedInputError{"Incoming DBus message is missing the required 'INTERFACE' header field"};
        }
      }

      auto const memberIt =
          std::ranges::find_if(headerFieldData, [](DBusMessageHeader::HeaderFieldReplyData const& data)
                               { return data.code == HeaderFieldCode::MEMBER; });
      if (memberIt == headerFieldData.cend())
      {
        auto const requiredHeaderFieldIt = std::ranges::find_if(
            HEADER_FIELDS, [](HeaderField const& field) { return field.decimalCode == HeaderFieldCode::MEMBER; });
        assert(requiredHeaderFieldIt != std::ranges::end(HEADER_FIELDS));
        if (std::ranges::contains(requiredHeaderFieldIt->requiredMessageType, data.messageType))
        {
          throw DBusMalformedInputError{"Incoming DBus message is missing the required 'MEMBER' header field"};
        }
      }

      auto const senderIt =
          std::ranges::find_if(headerFieldData, [](DBusMessageHeader::HeaderFieldReplyData const& data)
                               { return data.code == HeaderFieldCode::SENDER; });
      if (senderIt == headerFieldData.cend())
      {
        auto const requiredHeaderFieldIt = std::ranges::find_if(
            HEADER_FIELDS, [](HeaderField const& field) { return field.decimalCode == HeaderFieldCode::SENDER; });
        assert(requiredHeaderFieldIt != std::ranges::end(HEADER_FIELDS));
        if (std::ranges::contains(requiredHeaderFieldIt->requiredMessageType, data.messageType))
        {
          throw DBusMalformedInputError{"Incoming DBus message is missing the required 'SENDER' header field"};
        }
      }

      auto const destinationIt =
          std::ranges::find_if(headerFieldData, [](DBusMessageHeader::HeaderFieldReplyData const& data)
                               { return data.code == HeaderFieldCode::DESTINATION; });
      if (destinationIt == headerFieldData.cend())
      {
        auto const requiredHeaderFieldIt = std::ranges::find_if(
            HEADER_FIELDS, [](HeaderField const& field) { return field.decimalCode == HeaderFieldCode::DESTINATION; });
        assert(requiredHeaderFieldIt != std::ranges::end(HEADER_FIELDS));
        if (std::ranges::contains(requiredHeaderFieldIt->requiredMessageType, data.messageType))
        {
          throw DBusMalformedInputError{"Incoming DBus message is missing the required 'DESTINATION' header field"};
        }
      }

      auto const errorNameIt =
          std::ranges::find_if(headerFieldData, [](DBusMessageHeader::HeaderFieldReplyData const& data)
                               { return data.code == HeaderFieldCode::ERROR_NAME; });
      if (errorNameIt == headerFieldData.cend())
      {
        auto const requiredHeaderFieldIt = std::ranges::find_if(
            HEADER_FIELDS, [](HeaderField const& field) { return field.decimalCode == HeaderFieldCode::ERROR_NAME; });
        assert(requiredHeaderFieldIt != std::ranges::end(HEADER_FIELDS));
        if (std::ranges::contains(requiredHeaderFieldIt->requiredMessageType, data.messageType))
        {
          throw DBusMalformedInputError{"Incoming DBus message is missing the required 'ERROR_NAME' header field"};
        }
      }

      data.errorName = errorNameIt == headerFieldData.cend()
                           ? std::nullopt
                           : std::optional{errorNameIt->data.UnmarshalData<std::string>()};
      data.destination = destinationIt == headerFieldData.cend()
                             ? std::nullopt
                             : std::optional{destinationIt->data.UnmarshalData<std::string>()};
      data.sender = senderIt == headerFieldData.cend() ? std::nullopt
                                                       : std::optional{senderIt->data.UnmarshalData<std::string>()};
      data.objectPath = objectPathIt == headerFieldData.cend()
                            ? std::nullopt
                            : std::optional{objectPathIt->data.UnmarshalData<ObjectPath>()};
      data.interface = interfaceIt == headerFieldData.cend()
                           ? std::nullopt
                           : std::optional{interfaceIt->data.UnmarshalData<DBusInterfaceName>()};
      data.member = memberIt == headerFieldData.cend() ? std::nullopt
                                                       : std::optional{memberIt->data.UnmarshalData<std::string>()};
      data.replySerial =
          serialIt == headerFieldData.cend() ? std::nullopt : std::optional{serialIt->data.UnmarshalData<uint32_t>()};
      data.signature = signatureIt == headerFieldData.cend()
                           ? std::nullopt
                           : std::optional{signatureIt->data.UnmarshalData<Signature>()};
      data.headerFields = headerFieldData;
    }
  }  // namespace

  DBusMessageHeader::DBusMessageHeader(std::vector<byte> data)
    : m_data(UnmarshalDBusHeader(std::move(data)))
  {
  }

  uint32_t DBusMessageHeader::GetSerial() const
  {
    return m_data.serial;
  }

  std::optional<uint32_t> const& DBusMessageHeader::GetReplySerial() const
  {
    return m_data.replySerial;
  }

  DBusMessageType DBusMessageHeader::GetMessageType() const
  {
    return m_data.messageType;
  }

  uint32_t DBusMessageHeader::GetHeaderFieldsLength() const
  {
    return m_data.headerFieldLength;
  }

  uint32_t DBusMessageHeader::GetMessageLength() const
  {
    return m_data.messageLength;
  }

  std::optional<Signature> const& DBusMessageHeader::GetSignature() const
  {
    return m_data.signature;
  }

  std::optional<ObjectPath> const& DBusMessageHeader::GetObjectPath() const
  {
    return m_data.objectPath;
  }

  std::optional<DBusInterfaceName> const& DBusMessageHeader::GetInterface() const
  {
    return m_data.interface;
  }

  std::optional<std::string> const& DBusMessageHeader::GetMember() const
  {
    return m_data.member;
  }

  std::optional<std::string> const& DBusMessageHeader::GetSender() const
  {
    return m_data.sender;
  }

  std::optional<std::string> const& DBusMessageHeader::GetDestination() const
  {
    return m_data.destination;
  }

  std::optional<std::string> const& DBusMessageHeader::GetErrorName() const
  {
    return m_data.errorName;
  }

  void DBusMessageHeader::ParseHeaderFieldLength(std::vector<byte> data)
  {
    m_data.headerFieldLength = UnmarshalDBusType<uint32_t>(data, "u");
  }

  void DBusMessageHeader::ParseRemainderOfHeader(std::vector<byte> const& data, uint32_t& arrPointer)
  {
    UnmarshalDBusHeader(data, m_data, arrPointer);
  }

  IncomingDBusMessage::IncomingDBusMessage(DBusMessageHeader header, std::vector<byte> messageBody)
    : m_messageBody(std::move(messageBody))
    , m_header(std::move(header))
  {
  }

  DBusMessageHeader const& IncomingDBusMessage::GetHeader() const
  {
    return m_header;
  }

  bool IncomingDBusMessage::HasArguments() const
  {
    return !m_messageBody.empty();
  }

  std::vector<byte> const& IncomingDBusMessage::GetRawData() const
  {
    return m_messageBody;
  }

  std::string IncomingDBusMessage::GetInfo() const
  {
    std::string info{std::format(
        "IncomingDBusMessage Info: Message Type: {}, Method: {}, Path: {}, Interface: {}, Destination: {}, Signature: "
        "{}, Error Name: {}, Reply Serial: {}",
        magic_enum::enum_name(m_header.GetMessageType()), m_header.GetMember().value_or("N/A"),
        m_header.GetObjectPath().has_value() ? m_header.GetObjectPath()->GetPath() : "N/A",
        m_header.GetInterface().has_value() ? m_header.GetInterface()->GetName() : "N/A",
        m_header.GetDestination().value_or("N/A"),
        m_header.GetSignature().has_value() ? m_header.GetSignature()->GetSignature() : "N/A",
        m_header.GetErrorName().value_or("N/A"),
        m_header.GetReplySerial().has_value() ? std::to_string(*m_header.GetReplySerial()) : "N/A")};

    return info;
  }
}  // namespace cxxbus
