#include "DBusReply.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <iterator>
#include <ranges>

#include "DBus.h"

namespace
{
  DBusReplyHeader::ReplyData UnmarshalDBusHeader(std::vector<byte> dbusMessage)
  {
    // In this function we parse everything up until the array of variants, BUT INCLUDING the length of the array of variants
    // That way, we know how many bytes to read in always (16 at first, followed by the length of the header fields, followed by padding + length of message)

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
    // 2nd uint32_t is the serial of this message, used as a cookie by the sender to identify the reply correspending to this request.
    // Must be non-zero value Array of struct of byte, variant are the header fields.
    // The message type specifies which fields are required

    // We also parse the header field array length as the final u32. Slightly cursed, but works
    auto header = UnmarshalDBusType<MultipleCompleteTypes<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t, uint32_t>>(
        std::ranges::to<std::vector>(dbusMessage | std::views::take(FIRST_HEADER_PART_SIZE)), "yyyyuuu");

    return {.serial = 0,
            .messageLength = header.GetType<4>(),
            .headerFieldLength = header.GetType<6>(),
            .signature = "",
            .headerFields = {}};
  }

  void UnmarshalDBusHeader(std::vector<byte> dbusMessage, DBusReplyHeader::ReplyData & data)
  {
    uint32_t arrPointer{};

    auto headerFields = UnmarshalDBusType<std::vector<std::tuple<uint8_t, Variant>>>(
        std::ranges::to<std::vector>(dbusMessage | std::views::take(data.headerFieldLength)), "a(yv)");

    std::vector<DBusReplyHeader::HeaderFieldReplyData> headerFieldData{};
    std::ranges::transform(
        headerFields, std::back_inserter(headerFieldData), [](std::tuple<uint8_t, Variant> const& headerData)
        { return DBusReplyHeader::HeaderFieldReplyData{.code = static_cast<HeaderFieldCode>(std::get<0>(headerData)), .data = std::get<1>(headerData)}; });

    auto const signatureIt =
        std::ranges::find_if(headerFieldData, [](DBusReplyHeader::HeaderFieldReplyData const& data) { return data.code == HeaderFieldCode::SIGNATURE; });
    if (signatureIt == headerFieldData.cend())
    {
      // No signature provided, so this means our message MUST be empty
      if (data.messageLength != 0)
      {
        throw DBusMalformedInputError{"Incoming DBus message did not specify a signature while providing a non-zero body."};
      }
    }

    auto const serialIt =
        std::ranges::find_if(headerFieldData, [](DBusReplyHeader::HeaderFieldReplyData const& data) { return data.code == HeaderFieldCode::REPLY_SERIAL; });
    if (serialIt == headerFieldData.cend())
    {
      throw DBusMalformedInputError{"Incoming DBus message did not specify a REPLY_SERIAL header field."};
    }

    data.serial = serialIt->data.UnmarshalData<uint32_t>();
    data.signature = signatureIt == headerFieldData.cend() ? "" : signatureIt->data.UnmarshalData<std::string>();
    data.headerFields = headerFieldData;
  }
}  // namespace

DBusReplyHeader::DBusReplyHeader(std::vector<byte> data)
  : m_data(UnmarshalDBusHeader(std::move(data)))
{
}

uint32_t DBusReplyHeader::GetSerial() const
{
  return m_data.serial;
}

uint32_t DBusReplyHeader::GetHeaderFieldsLength() const
{
  return m_data.headerFieldLength;
}

uint32_t DBusReplyHeader::GetMessageLength() const
{
  return m_data.messageLength;
}

std::string const& DBusReplyHeader::GetSignature() const
{
  return m_data.signature;
}

void DBusReplyHeader::ParseRemainderOfHeader(std::vector<byte> data)
{
  UnmarshalDBusHeader(std::move(data), m_data);
}

DBusReply::DBusReply() = default;

DBusReply::DBusReply(DBusReplyHeader header, std::vector<byte> data)
  : m_header(std::move(header))
  , m_data(std::move(data))
{
}

uint32_t DBusReply::GetSerial() const
{
  return m_header.GetSerial();
}