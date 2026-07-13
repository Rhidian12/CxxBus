#pragma once

#include <stdexcept>

#include "DBus.h"

class DBusReplyHeader
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
    uint32_t messageLength;
    uint32_t headerFieldLength;
    std::string signature;
    std::vector<HeaderFieldReplyData> headerFields;
  };

private:
  ReplyData m_data;

public:
  DBusReplyHeader() = default;
  DBusReplyHeader(std::vector<byte> data);

  uint32_t GetSerial() const;
  uint32_t GetHeaderFieldsLength() const;
  uint32_t GetMessageLength() const;
  std::string const & GetSignature() const;

  void ParseRemainderOfHeader(std::vector<byte> data);
};

class DBusReply
{
 private:
  std::vector<byte> m_messageBody;

 private:
  DBusReplyHeader m_header;
  std::vector<byte> m_data;

 public:
  DBusReply();
  DBusReply(DBusReplyHeader header, std::vector<byte> data);

  template <IsDBusType T>
  T Get() const;

  uint32_t GetSerial() const;
};

template <IsDBusType T>
T DBusReply::Get() const
{
  return UnmarshalDBusType<T>(m_data, m_header.GetSignature());
}