#include <gtest/gtest.h>

#include "DBus.h"
#include "DBusMessage.h"
#include "DBusReply.h"
#include "DBusTypes.h"

namespace
{
  DBusMessage ParseFullMessage(std::vector<byte> const& fullMessageBytes)
  {
    uint32_t arrPointer{};
    DBusMessageHeader header{std::ranges::to<std::vector>(fullMessageBytes | std::views::take(FIRST_HEADER_PART_SIZE))};

    arrPointer += FIRST_HEADER_PART_SIZE;

    uint32_t const headerFieldsLength{
        UnmarshalDBusType<uint32_t>(std::ranges::to<std::vector>(fullMessageBytes | std::views::drop(arrPointer) | std::views::take(sizeof(uint32_t))), "u")};

    header.ParseRemainderOfHeader(fullMessageBytes, headerFieldsLength, arrPointer);

    AddPaddingToSize(arrPointer, DBUS_MESSAGE_BODY_ALIGNMENT);

    return DBusMessage{std::move(header), std::ranges::to<std::vector>(fullMessageBytes | std::views::drop(arrPointer))};
  }
}  // namespace

struct DBusMessageTestSuite : ::testing::Test
{
};

// ---------------------------------------------------------------------
// SERIALIZATION
// ---------------------------------------------------------------------
// clang-format off
TEST_F(DBusMessageTestSuite, SerializeHelloMessage)
{
  // DBusMessage{"Hello", "/org/freedesktop/DBus", "org.freedesktop.DBus"}
  // with serial = 1, no body.
  //
  // Fixed header (offsets 0-11):
  //   0:     'l' -- little-endian
  //   1:     1   -- METHOD_CALL
  //   2:     0   -- flags
  //   3:     1   -- protocol version
  //   4-7:   body length = 0
  //   8-11:  serial = 1
  //
  // Header fields array (a(yv)), length field at offset 12-15:
  //   PATH      (code 1, "o", "/org/freedesktop/DBus")  starts at 16
  //   INTERFACE (code 2, "s", "org.freedesktop.DBus")   starts at 48
  //             (2 bytes padding before it, offset 46 -> 48)
  //   MEMBER    (code 3, "s", "Hello")                  starts at 80
  //             (3 bytes padding before it, offset 77 -> 80)
  //   header fields end at offset 94; total header fields length = 78
  //
  // 2 bytes of padding (offset 94-95) bring the body start to offset
  // 96, an 8-byte boundary -- required even though the body is empty.
  // Total message length: 96 bytes.
  DBusMessage msg{"Hello", ObjectPath{"/org/freedesktop/DBus"}, "org.freedesktop.DBus"};
 
  EXPECT_EQ(msg.Serialize(/*serial=*/1),
            (std::vector<byte>{
                // Fixed header
                'l', 0x01, 0x00, 0x01,               // endian, type, flags, version
                0x00, 0x00, 0x00, 0x00,               // body length = 0
                0x01, 0x00, 0x00, 0x00,               // serial = 1
                0x4E, 0x00, 0x00, 0x00,               // header fields length = 78
 
                // PATH field (code 1, "o")
                0x01, 0x01, 'o', 0x00,                // code, sig len, 'o', NUL
                0x15, 0x00, 0x00, 0x00,               // path length = 21
                '/', 'o', 'r', 'g', '/', 'f', 'r', 'e', 'e', 'd', 'e',
                's', 'k', 't', 'o', 'p', '/', 'D', 'B', 'u', 's',       // "/org/freedesktop/DBus"
                0x00,                                  // NUL
 
                0x00, 0x00,                            // pad to offset 48
 
                // INTERFACE field (code 2, "s")
                0x02, 0x01, 's', 0x00,                // code, sig len, 's', NUL
                0x14, 0x00, 0x00, 0x00,               // string length = 20
                'o', 'r', 'g', '.', 'f', 'r', 'e', 'e', 'd', 'e', 's',
                'k', 't', 'o', 'p', '.', 'D', 'B', 'u', 's',            // "org.freedesktop.DBus"
                0x00,                                  // NUL
 
                0x00, 0x00, 0x00,                      // pad to offset 80
 
                // MEMBER field (code 3, "s")
                0x03, 0x01, 's', 0x00,                // code, sig len, 's', NUL
                0x05, 0x00, 0x00, 0x00,               // string length = 5
                'H', 'e', 'l', 'l', 'o',               // "Hello"
                0x00,                                  // NUL
 
                0x00, 0x00,                            // pad to offset 96 (body start; body is empty)
            }));
}
 
TEST_F(DBusMessageTestSuite, SerializeMessageWithBodyIncludesSignatureField)
{
  // A method call that carries a body: path "/o", interface "i",
  // member "M" (deliberately short to keep the byte-counting simple
  // -- not meant to be spec-realistic names), body = uint32_t(7),
  // signature "u", serial = 5.
  //
  // Body length = 4, so the SIGNATURE header field (code 8, "g") must
  // be present -- it's how a reader knows how to interpret the body
  // bytes that follow.
  DBusMessage msg{MultipleCompleteTypes<uint32_t>(static_cast<uint32_t>(7)), "M", ObjectPath{"/o"}, "i"};
 
  EXPECT_EQ(msg.Serialize(/*serial=*/5),
            (std::vector<byte>{
                // Fixed header
                'l', 0x01, 0x00, 0x01,   // endian, type, flags, version
                0x04, 0x00, 0x00, 0x00,  // body length = 4
                0x05, 0x00, 0x00, 0x00,  // serial = 5
                0x37, 0x00, 0x00, 0x00,  // header fields length = 55
 
                // PATH field (code 1, "o") -- starts at offset 16
                0x01, 0x01, 'o', 0x00,
                0x02, 0x00, 0x00, 0x00,  // path length = 2
                '/', 'o',
                0x00,
 
                0x00, 0x00, 0x00, 0x00, 0x00,  // pad to offset 32
 
                // INTERFACE field (code 2, "s") -- starts at offset 32
                0x02, 0x01, 's', 0x00,
                0x01, 0x00, 0x00, 0x00,  // string length = 1
                'i',
                0x00,
 
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // pad to offset 48
 
                // MEMBER field (code 3, "s") -- starts at offset 48
                0x03, 0x01, 's', 0x00,
                0x01, 0x00, 0x00, 0x00,  // string length = 1
                'M',
                0x00,
 
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // pad to offset 64
 
                // SIGNATURE field (code 8, "g") -- starts at offset 64
                0x08, 0x01, 'g', 0x00,
                0x01, 'u', 0x00,  // signature length = 1, "u", NUL
 
                0x00,  // pad to offset 72 (body start)
 
                // Body: uint32_t = 7
                0x07, 0x00, 0x00, 0x00,
            }));
}
 
// ---------------------------------------------------------------------
// DESERIALIZATION
// ---------------------------------------------------------------------
 
TEST_F(DBusMessageTestSuite, DeserializeHelloMessageRoundTrip)
{
  // Reuses the exact bytes verified in SerializeHelloMessage above --
  // parsing our own serialized output back should recover the
  // original fields exactly.
  std::vector<byte> bytes{
      'l', 0x01, 0x00, 0x01,
      0x00, 0x00, 0x00, 0x00,
      0x01, 0x00, 0x00, 0x00,
      0x4E, 0x00, 0x00, 0x00,
 
      0x01, 0x01, 'o', 0x00,
      0x15, 0x00, 0x00, 0x00,
      '/', 'o', 'r', 'g', '/', 'f', 'r', 'e', 'e', 'd', 'e',
      's', 'k', 't', 'o', 'p', '/', 'D', 'B', 'u', 's',
      0x00,
 
      0x00, 0x00,
 
      0x02, 0x01, 's', 0x00,
      0x14, 0x00, 0x00, 0x00,
      'o', 'r', 'g', '.', 'f', 'r', 'e', 'e', 'd', 'e', 's',
      'k', 't', 'o', 'p', '.', 'D', 'B', 'u', 's',
      0x00,
 
      0x00, 0x00, 0x00,
 
      0x03, 0x01, 's', 0x00,
      0x05, 0x00, 0x00, 0x00,
      'H', 'e', 'l', 'l', 'o',
      0x00,
 
      0x00, 0x00,
  };
  ASSERT_EQ(bytes.size(), 96u);
 
  DBusMessage message = ParseFullMessage(bytes);
  auto const& header = message.GetHeader();
 
  EXPECT_EQ(header.GetMessageType(), DBusMessageType::METHOD_CALL);
  EXPECT_EQ(header.GetSerial(), 1u);
  EXPECT_EQ(header.GetHeaderFieldsLength(), 78u);
  EXPECT_EQ(header.GetMessageLength(), 0u);
  EXPECT_EQ(header.GetObjectPath(), ObjectPath("/org/freedesktop/DBus"));
  EXPECT_EQ(header.GetInterface(), "org.freedesktop.DBus");
  EXPECT_EQ(header.GetMember(), "Hello");
  EXPECT_FALSE(header.GetSignature().has_value());  // no body -> no SIGNATURE field
  EXPECT_TRUE(message.GetRawData().empty());
}
 
TEST_F(DBusMessageTestSuite, DeserializeMethodReturnReplyWithBody)
{
  // A METHOD_RETURN reply to the Hello call above: REPLY_SERIAL = 1
  // (referring back to the call's serial), SIGNATURE = "s", body =
  // the assigned unique bus name ":1.42". This message's own serial
  // is 2 (its own position in the sender's outgoing message stream,
  // unrelated to the serial it's replying to).
  std::vector<byte> bytes{
      // Fixed header
      'l', 0x02, 0x00, 0x01,   // endian, type=METHOD_RETURN, flags, version
      0x0A, 0x00, 0x00, 0x00,  // body length = 10
      0x02, 0x00, 0x00, 0x00,  // serial = 2
      0x0F, 0x00, 0x00, 0x00,  // header fields length = 15
 
      // REPLY_SERIAL field (code 5, "u") -- starts at offset 16
      0x05, 0x01, 'u', 0x00,
      0x01, 0x00, 0x00, 0x00,  // reply_serial = 1
 
      // SIGNATURE field (code 8, "g") -- starts at offset 24 (already
      // 8-aligned, no padding needed before it)
      0x08, 0x01, 'g', 0x00,
      0x01, 's', 0x00,  // signature length = 1, "s", NUL
 
      0x00,  // pad to offset 32 (body start)
 
      // Body: STRING ":1.42"
      0x05, 0x00, 0x00, 0x00,             // string length = 5
      ':', '1', '.', '4', '2',             // ":1.42"
      0x00,                                // NUL
  };
  ASSERT_EQ(bytes.size(), 42u);
 
  DBusMessage message = ParseFullMessage(bytes);
  auto const& header = message.GetHeader();
 
  EXPECT_EQ(header.GetMessageType(), DBusMessageType::METHOD_RETURN);  // METHOD_RETURN
  EXPECT_EQ(header.GetSerial(), 2u);
  EXPECT_EQ(header.GetHeaderFieldsLength(), 15u);
  EXPECT_EQ(header.GetMessageLength(), 10u);
 
  ASSERT_TRUE(header.GetReplySerial().has_value());
  EXPECT_EQ(header.GetReplySerial().value(), 1u);
 
  ASSERT_TRUE(header.GetSignature().has_value());
  EXPECT_EQ(header.GetSignature().value(), Signature("s"));
 
  // Raw body bytes, and the same bytes decoded through the
  // unmarshalling path using the signature we just read out of the
  // header -- ties this back to the UnmarshalDBusType tests.
  std::vector<byte> expectedBody{0x05, 0x00, 0x00, 0x00, ':', '1', '.', '4', '2', 0x00};
  EXPECT_EQ(message.GetRawData(), expectedBody);
  EXPECT_EQ(UnmarshalDBusType<std::string>(message.GetRawData(), header.GetSignature()->GetSignature()),
            ":1.42");
}
 
// ---------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------
 
TEST_F(DBusMessageTestSuite, ThrowsWhenFirstHeaderPartIsTooShort)
{
  std::vector<byte> tooShort{'l', 0x01, 0x00, 0x01};  // only 4 of 16 bytes
  EXPECT_THROW(DBusMessageHeader{std::move(tooShort)}, DBusMalformedInputError);
}
 
TEST_F(DBusMessageTestSuite, ThrowsWhenHeaderFieldsPartDoesNotMatchDeclaredLength)
{
  // Fixed header declares header fields length = 78 (matching the
  // Hello message above), but we only supply 4 bytes of header
  // fields data.
  std::vector<byte> firstPart{
      'l', 0x01, 0x00, 0x01,
      0x00, 0x00, 0x00, 0x00,
      0x01, 0x00, 0x00, 0x00,
      0x4E, 0x00, 0x00, 0x00,  // header fields length = 78
  };
  DBusMessageHeader header{std::ranges::to<std::vector>(firstPart | std::views::take(FIRST_HEADER_PART_SIZE))};
  
  std::vector<byte> tooShortFields{0x01, 0x01, 'o', 0x00};
  uint32_t arrPointer{};
  EXPECT_THROW(header.ParseRemainderOfHeader(std::move(tooShortFields), 78, arrPointer), DBusMalformedInputError);
  ASSERT_EQ(header.GetHeaderFieldsLength(), 78u);
}
// clang-format on