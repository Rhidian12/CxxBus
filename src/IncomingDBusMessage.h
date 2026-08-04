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

#pragma once

#include <optional>

#include "DBus.h"
#include "DBusTypes.h"

namespace cxxbus
{
  class DBusMessageHeader
  {
   public:
    struct HeaderFieldReplyData
    {
      HeaderFieldCode code;
      Variant data;

      bool operator==(HeaderFieldReplyData const&) const noexcept = default;
    };

    struct ReplyData
    {
      uint32_t serial;
      std::optional<uint32_t> replySerial;
      DBusMessageType messageType;
      std::optional<ObjectPath> objectPath;
      std::optional<DBusInterfaceName> interface;
      std::optional<std::string> member;
      std::optional<std::string> errorName;
      std::optional<Signature> signature;
      std::optional<std::string> sender;
      std::optional<std::string> destination;
      uint32_t messageLength;
      uint32_t headerFieldLength;
      std::vector<HeaderFieldReplyData> headerFields;

      bool operator==(ReplyData const&) const noexcept = default;
    };

   private:
    ReplyData m_data;

   public:
    DBusMessageHeader() = default;
    DBusMessageHeader(std::vector<byte> data);

    uint32_t GetSerial() const;
    std::optional<uint32_t> const& GetReplySerial() const;
    DBusMessageType GetMessageType() const;
    uint32_t GetHeaderFieldsLength() const;
    uint32_t GetMessageLength() const;
    std::optional<Signature> const& GetSignature() const;
    std::optional<ObjectPath> const& GetObjectPath() const;
    std::optional<DBusInterfaceName> const& GetInterface() const;
    // Either method name or signal name, depending on message type
    std::optional<std::string> const& GetMember() const;
    std::optional<std::string> const& GetSender() const;
    std::optional<std::string> const& GetDestination() const;
    std::optional<std::string> const& GetErrorName() const;

    void ParseHeaderFieldLength(std::vector<byte> data);
    void ParseRemainderOfHeader(std::vector<byte> const& data, uint32_t& arrPointer);

    bool operator==(DBusMessageHeader const&) const noexcept = default;
  };

  class IncomingDBusMessage
  {
   private:
    std::vector<uint8_t> m_messageBody;
    DBusMessageHeader m_header;

   public:
    IncomingDBusMessage(DBusMessageHeader header, std::vector<byte> messageBody);
    IncomingDBusMessage() = default;

    DBusMessageHeader const& GetHeader() const;

    template <IsDBusType T>
    T Get() const
    {
      return UnmarshalDBusType<T>(m_messageBody, m_header.GetSignature()->GetSignature());
    }

    // Do we have any arguments in our message body?
    // i.e. is the message body empty or not?
    bool HasArguments() const;

    // Only useful for debugging purposes
    std::vector<byte> const& GetRawData() const;

    bool operator==(IncomingDBusMessage const&) const noexcept = default;
  };
}  // namespace cxxbus
