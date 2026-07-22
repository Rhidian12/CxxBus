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

  void ParseHeaderFieldLength(std::vector<byte> data);
  void ParseRemainderOfHeader(std::vector<byte> const& data, uint32_t& arrPointer);
};
