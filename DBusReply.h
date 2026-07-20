#pragma once

#include <optional>
#include <stdexcept>

#include "DBus.h"
#include "DBusTypes.h"

class DBusMessageHeader
{
 public:
  struct HeaderFieldReplyData
  {
    HeaderFieldCode code;
    Variant data;
  };

  struct ReplyData
  {
    uint32_t serial;
    std::optional<uint32_t> replySerial;
    DBusMessageType messageType;
    std::optional<ObjectPath> objectPath;
    std::optional<std::string> interface;
    std::optional<std::string> member;
    std::optional<Signature> signature;
    uint32_t messageLength;
    uint32_t headerFieldLength;
    std::vector<HeaderFieldReplyData> headerFields;
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
  std::optional<std::string> const& GetInterface() const;
  // Either method name or signal name, depending on message type
  std::optional<std::string> const& GetMember() const;

  void ParseRemainderOfHeader(std::vector<byte> const & data, uint32_t headerFieldLength, uint32_t & arrPointer);
};

class DBusReply
{
 private:
  std::vector<byte> m_messageBody;

 private:
  DBusMessageHeader m_header;
  std::vector<byte> m_data;

 public:
  DBusReply();
  DBusReply(DBusMessageHeader header, std::vector<byte> data);

  template <IsDBusType T>
  T Get() const;

  std::optional<uint32_t> const& GetReplySerial() const;
  DBusMessageHeader const& GetHeader() const;

  // Only useful for debugging purposes
  std::vector<byte> const& GetRawData() const;
};

template <IsDBusType T>
T DBusReply::Get() const
{
  return UnmarshalDBusType<T>(m_data, m_header.GetSignature()->GetSignature());
}