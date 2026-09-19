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

#include <sys/types.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "DBus.h"
#include "DBusTypes.h"
#include "magic_enum.hpp"

namespace cxxbus
{
  namespace
  {
    DBusMessageHeader::ReplyData UnmarshalDBusHeader(std::span<byte const> dbusMessage, uint32_t serial,
                                                     DBusMessageType messageType, uint32_t headerFieldLength,
                                                     uint32_t messageLength)
    {
      uint32_t arrPointer{FIRST_HEADER_PART_SIZE - sizeof(uint32_t)};
      std::vector<std::tuple<uint8_t, FastVariant>> headerFields =
          UnmarshalDBusType<std::vector<std::tuple<uint8_t, FastVariant>>>(dbusMessage, "a(yv)", arrPointer);

      auto const signatureIt = std::ranges::find_if(
          headerFields, [](std::tuple<uint8_t, FastVariant> const& data)
          { return static_cast<HeaderFieldCode>(std::get<0>(data)) == HeaderFieldCode::SIGNATURE; });
      if (signatureIt == headerFields.cend())
      {
        // No signature provided, so this means our message MUST be empty
        if (messageLength != 0) [[unlikely]]
        {
          throw DBusMalformedInputError{
              "Incoming DBus message did not specify a signature while providing a non-zero body."};
        }
      }

      auto const serialIt = std::ranges::find_if(
          headerFields, [](std::tuple<uint8_t, FastVariant> const& data)
          { return static_cast<HeaderFieldCode>(std::get<0>(data)) == HeaderFieldCode::REPLY_SERIAL; });
      if (serialIt == headerFields.cend())
      {
        auto const requiredHeaderFieldIt = std::ranges::find_if(
            HEADER_FIELDS, [](HeaderField const& field) { return field.decimalCode == HeaderFieldCode::REPLY_SERIAL; });
        assert(requiredHeaderFieldIt != std::ranges::end(HEADER_FIELDS));
        if (std::ranges::contains(requiredHeaderFieldIt->requiredMessageType, messageType))
        {
          throw DBusMalformedInputError{"Incoming DBus message is missing the required 'REPLY_SERIAL' header field"};
        }
      }

      auto const objectPathIt =
          std::ranges::find_if(headerFields, [](std::tuple<uint8_t, FastVariant> const& data)
                               { return static_cast<HeaderFieldCode>(std::get<0>(data)) == HeaderFieldCode::PATH; });
      if (objectPathIt == headerFields.cend())
      {
        auto const requiredHeaderFieldIt = std::ranges::find_if(
            HEADER_FIELDS, [](HeaderField const& field) { return field.decimalCode == HeaderFieldCode::PATH; });
        assert(requiredHeaderFieldIt != std::ranges::end(HEADER_FIELDS));
        if (std::ranges::contains(requiredHeaderFieldIt->requiredMessageType, messageType))
        {
          throw DBusMalformedInputError{"Incoming DBus message is missing the required 'PATH' header field"};
        }
      }

      auto const interfaceIt = std::ranges::find_if(
          headerFields, [](std::tuple<uint8_t, FastVariant> const& data)
          { return static_cast<HeaderFieldCode>(std::get<0>(data)) == HeaderFieldCode::INTERFACE; });
      if (interfaceIt == headerFields.cend())
      {
        auto const requiredHeaderFieldIt = std::ranges::find_if(
            HEADER_FIELDS, [](HeaderField const& field) { return field.decimalCode == HeaderFieldCode::INTERFACE; });
        assert(requiredHeaderFieldIt != std::ranges::end(HEADER_FIELDS));
        if (std::ranges::contains(requiredHeaderFieldIt->requiredMessageType, messageType))
        {
          throw DBusMalformedInputError{"Incoming DBus message is missing the required 'INTERFACE' header field"};
        }
      }

      auto const memberIt =
          std::ranges::find_if(headerFields, [](std::tuple<uint8_t, FastVariant> const& data)
                               { return static_cast<HeaderFieldCode>(std::get<0>(data)) == HeaderFieldCode::MEMBER; });
      if (memberIt == headerFields.cend())
      {
        auto const requiredHeaderFieldIt = std::ranges::find_if(
            HEADER_FIELDS, [](HeaderField const& field) { return field.decimalCode == HeaderFieldCode::MEMBER; });
        assert(requiredHeaderFieldIt != std::ranges::end(HEADER_FIELDS));
        if (std::ranges::contains(requiredHeaderFieldIt->requiredMessageType, messageType))
        {
          throw DBusMalformedInputError{"Incoming DBus message is missing the required 'MEMBER' header field"};
        }
      }

      auto const senderIt =
          std::ranges::find_if(headerFields, [](std::tuple<uint8_t, FastVariant> const& data)
                               { return static_cast<HeaderFieldCode>(std::get<0>(data)) == HeaderFieldCode::SENDER; });
      if (senderIt == headerFields.cend())
      {
        auto const requiredHeaderFieldIt = std::ranges::find_if(
            HEADER_FIELDS, [](HeaderField const& field) { return field.decimalCode == HeaderFieldCode::SENDER; });
        assert(requiredHeaderFieldIt != std::ranges::end(HEADER_FIELDS));
        if (std::ranges::contains(requiredHeaderFieldIt->requiredMessageType, messageType))
        {
          throw DBusMalformedInputError{"Incoming DBus message is missing the required 'SENDER' header field"};
        }
      }

      auto const destinationIt = std::ranges::find_if(
          headerFields, [](std::tuple<uint8_t, FastVariant> const& data)
          { return static_cast<HeaderFieldCode>(std::get<0>(data)) == HeaderFieldCode::DESTINATION; });
      if (destinationIt == headerFields.cend())
      {
        auto const requiredHeaderFieldIt = std::ranges::find_if(
            HEADER_FIELDS, [](HeaderField const& field) { return field.decimalCode == HeaderFieldCode::DESTINATION; });
        assert(requiredHeaderFieldIt != std::ranges::end(HEADER_FIELDS));
        if (std::ranges::contains(requiredHeaderFieldIt->requiredMessageType, messageType))
        {
          throw DBusMalformedInputError{"Incoming DBus message is missing the required 'DESTINATION' header field"};
        }
      }

      auto const errorNameIt = std::ranges::find_if(
          headerFields, [](std::tuple<uint8_t, FastVariant> const& data)
          { return static_cast<HeaderFieldCode>(std::get<0>(data)) == HeaderFieldCode::ERROR_NAME; });
      if (errorNameIt == headerFields.cend())
      {
        auto const requiredHeaderFieldIt = std::ranges::find_if(
            HEADER_FIELDS, [](HeaderField const& field) { return field.decimalCode == HeaderFieldCode::ERROR_NAME; });
        assert(requiredHeaderFieldIt != std::ranges::end(HEADER_FIELDS));
        if (std::ranges::contains(requiredHeaderFieldIt->requiredMessageType, messageType))
        {
          throw DBusMalformedInputError{"Incoming DBus message is missing the required 'ERROR_NAME' header field"};
        }
      }

      return {.serial = serial,
              .replySerial = serialIt == headerFields.cend()
                                 ? std::nullopt
                                 : std::optional{std::get<1>(*serialIt).UnmarshalData<uint32_t>()},
              .messageType = messageType,
              .objectPath = objectPathIt == headerFields.cend()
                                ? std::nullopt
                                : std::optional{std::get<1>(*objectPathIt).UnmarshalData<ObjectPath>()},
              .interface = interfaceIt == headerFields.cend()
                               ? std::nullopt
                               : std::optional{std::get<1>(*interfaceIt).UnmarshalData<DBusInterfaceName>()},
              .member = memberIt == headerFields.cend()
                            ? std::nullopt
                            : std::optional{std::get<1>(*memberIt).UnmarshalData<std::string>()},
              .errorName = errorNameIt == headerFields.cend()
                               ? std::nullopt
                               : std::optional{std::get<1>(*errorNameIt).UnmarshalData<std::string>()},
              .signature = signatureIt == headerFields.cend()
                               ? std::nullopt
                               : std::optional{std::get<1>(*signatureIt).UnmarshalData<Signature>()},
              .sender = senderIt == headerFields.cend()
                            ? std::nullopt
                            : std::optional{std::get<1>(*senderIt).UnmarshalData<std::string>()},
              .destination = senderIt == headerFields.cend()
                                 ? std::nullopt
                                 : std::optional{std::get<1>(*senderIt).UnmarshalData<std::string>()},
              .messageLength = messageLength,
              .headerFieldLength = headerFieldLength};
    }
  }  // namespace

  DBusMessageHeader::DBusMessageHeader(std::span<byte const> data, uint32_t serial, DBusMessageType messageType,
                                       uint32_t headerFieldLength, uint32_t messageLength)
    : m_data(UnmarshalDBusHeader(data, serial, messageType, headerFieldLength, messageLength))
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
