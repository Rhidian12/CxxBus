#include "DBusReply.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <iterator>
#include <ranges>

#include "DBus.h"

namespace
{
  inline DBusReply::ReplyData UnmarshalDBusMessage(std::vector<byte> dbusMessage)
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

    uint32_t constexpr FIRST_HEADER_PART_SIZE = sizeof(uint8_t) * 4 + sizeof(uint32_t) * 2;

    uint32_t arrPointer{};
    auto header = UnmarshalDBusType<MultipleCompleteTypes<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t>>(
        std::ranges::to<std::vector>(dbusMessage | std::views::take(FIRST_HEADER_PART_SIZE)), "yyyyuu");

    // Skip the first part of the header from the message
    arrPointer += FIRST_HEADER_PART_SIZE;

    // How long is our array of structs?
    uint32_t const arrLength{
        UnmarshalDBusType<uint32_t>(std::ranges::to<std::vector>(dbusMessage | std::views::drop(arrPointer) | std::views::take(sizeof(uint32_t))), "u")};

    auto headerFields = UnmarshalDBusType<std::vector<std::tuple<uint8_t, DeserializedVariant<std::string>>>>(
        std::ranges::to<std::vector>(dbusMessage | std::views::drop(arrPointer) | std::views::take(arrLength)), "a(yv)");

    arrPointer += arrLength;

    // [TODO]: Not a magic number
    SkipPadding(arrPointer, 8);

    std::vector<DBusReply::HeaderFieldReplyData> headerFieldData{};
    std::ranges::transform(
        headerFields, std::back_inserter(headerFieldData), [](std::tuple<uint8_t, DeserializedVariant<std::string>> const& headerData)
        { return DBusReply::HeaderFieldReplyData{.code = static_cast<HeaderFieldCode>(std::get<0>(headerData)), .data = std::get<1>(headerData).Get()}; });

    auto const signatureIt =
        std::ranges::find_if(headerFieldData, [](DBusReply::HeaderFieldReplyData const& data) { return data.code == HeaderFieldCode::SIGNATURE; });
    if (signatureIt == headerFieldData.cend())
    {
      // No signature provided, so this means our message MUST be empty
      if (header.GetType<4>() != 0)
      {
        throw DBusMalformedInputError{"Incoming DBus message did not specify a signature while providing a non-zero body."};
      }
    }

    return {.serial = header.GetType<5>(),
            .signature = signatureIt == headerFieldData.cend() ? "" : signatureIt->data,
            .headerFields = headerFieldData,
            .messageBody = std::ranges::to<std::vector>(dbusMessage | std::views::drop(arrPointer))};
  }
}  // namespace

DBusReply::DBusReply() = default;

DBusReply::DBusReply(std::vector<byte> data)
  : m_data(UnmarshalDBusMessage(std::move(data)))
{
}
