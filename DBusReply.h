#pragma once

#include <stdexcept>

#include "DBus.h"

class DBusReply
{
 public:
  struct HeaderFieldReplyData
  {
    HeaderFieldCode code;
    std::string data;
  };

  struct ReplyData
  {
    uint32_t serial;
    std::string signature;
    std::vector<HeaderFieldReplyData> headerFields;
    std::vector<byte> messageBody;
  };

 private:
  ReplyData m_data;

 public:
  DBusReply();
  DBusReply(std::vector<byte> data);

  template <IsDeserializableDBusType T>
  T Get() const;
};

template <IsDeserializableDBusType T>
T DBusReply::Get() const
{
  return UnmarshalDBusType<T>(m_data.messageBody, m_data.signature);
}