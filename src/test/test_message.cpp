#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

#include "gtest/gtest.h"
#include "src/DBus.h"
#include "src/DBusMessage.h"
#include "src/DBusTypes.h"
#include "src/IncomingDBusMessage.h"
#include "src/MessageBuffer.h"

using namespace cxxbus;

namespace
{
  IncomingDBusMessage ParseFullMessage(std::vector<byte> const& fullMessageBytes)
  {
    auto headerData =
        UnmarshalDBusType<MultipleCompleteTypes<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t, uint32_t>>(
            std::ranges::to<std::vector>(fullMessageBytes | std::views::take(FIRST_HEADER_PART_SIZE)), "yyyyuuu");
    uint32_t const messageLength = headerData.GetType<4>();
    uint32_t const headerFieldArrLength = headerData.GetType<6>();
    uint32_t const serial = headerData.GetType<5>();
    DBusMessageType const messageType = static_cast<DBusMessageType>(headerData.GetType<1>());

    uint32_t remainingSizeToRead{FIRST_HEADER_PART_SIZE + headerFieldArrLength};
    uint32_t nrOfPaddingBytes = AddPaddingToSize(remainingSizeToRead, DBUS_MESSAGE_BODY_ALIGNMENT);

    MessageBuffer fullMessage{new byte[fullMessageBytes.size()]{},
                              MessageBufferDeleter{MessageBufferDeleter::NotPartOfPoolTag}};
    std::memcpy(fullMessage.get(), fullMessageBytes.data(), fullMessageBytes.size());

    return IncomingDBusMessage{
        DBusMessageHeader{
            std::span<byte const>{fullMessageBytes.begin(),
                                  fullMessageBytes.begin() + FIRST_HEADER_PART_SIZE + headerFieldArrLength},
            serial, messageType, headerFieldArrLength, messageLength},
        std::move(fullMessage), FIRST_HEADER_PART_SIZE + headerFieldArrLength + nrOfPaddingBytes};
  }
}  // namespace

struct DBusMessageTestSuite : ::testing::Test
{
};

::testing::AssertionResult VecEqual(std::vector<byte> const& a, std::vector<byte> const& b)
{
  if (a.size() != b.size())
  {
    std::optional<size_t> foundIndex{std::nullopt};
    for (size_t i{}; i < a.size(); ++i)
    {
      if (a[i] != b[i] || i >= b.size())
      {
        foundIndex = i;
        break;
      }
    }
    return ::testing::AssertionFailure() << "Size mismatch. LHS: " << a.size() << " vs RHS: " << b.size()
                                         << ". Vectors mismatch at index: " << foundIndex.value_or(a.size());
  }

  for (size_t i{}; i < a.size(); ++i)
  {
    if (a[i] != b[i])
    {
      return ::testing::AssertionFailure() << "Mismatch at index: " << i << ", LHS: " << a[i] << " vs RHS: " << b[i];
    }
  }

  return ::testing::AssertionSuccess();
}

TEST_F(DBusMessageTestSuite, SerializeHelloMessage)
{
  DBusMessage msg{DBusMessage::Method("Hello", ObjectPath{"/org/freedesktop/DBus"})
                      .Interface(DBusInterfaceName{"org.freedesktop.DBus"})};

  // clang-format off
  EXPECT_TRUE(VecEqual(msg.Serialize(/*serial=*/1),
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
            })));
  // clang-format on 
}
 
TEST_F(DBusMessageTestSuite, SerializeMessageWithBodyIncludesSignatureField)
{
  DBusMessage msg{DBusMessage::Method("M", ObjectPath{"/o"}).Interface(DBusInterfaceName{"com.dbus.CxxTest"}).Parameter(MultipleCompleteTypes<uint32_t>(static_cast<uint32_t>(7)))};
 
  // clang-format off
  EXPECT_TRUE(VecEqual(msg.Serialize(/*serial=*/5),
            (std::vector<byte>{
                // Fixed header
                'l', 0x01, 0x00, 0x01,   // endian, type, flags, version
                0x04, 0x00, 0x00, 0x00,  // body length = 4
                0x05, 0x00, 0x00, 0x00,  // serial = 5
                0x47, 0x00, 0x00, 0x00,  // header fields length = 55
 
                // PATH field (code 1, "o") -- starts at offset 16
                0x01, 0x01, 'o', 0x00,
                0x02, 0x00, 0x00, 0x00,  // path length = 2
                '/', 'o',
                0x00,
 
                0x00, 0x00, 0x00, 0x00, 0x00,  // pad to offset 32
 
                // INTERFACE field (code 2, "s") -- starts at offset 32
                0x02, 0x01, 's', 0x00,
                0x10, 0x00, 0x00, 0x00,  // string length = 16
                'c', 'o', 'm', '.', 'd', 'b', 'u', 's', '.', 'C', 'x', 'x', 'T', 'e', 's', 't',
                0x00,
 
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // pad to offset 64
 
                // MEMBER field (code 3, "s") -- starts at offset 64
                0x03, 0x01, 's', 0x00,
                0x01, 0x00, 0x00, 0x00,  // string length = 1
                'M',
                0x00,
 
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // pad to offset 80
 
                // SIGNATURE field (code 8, "g") -- starts at offset 80
                0x08, 0x01, 'g', 0x00,
                0x01, 'u', 0x00,  // signature length = 1, "u", NUL
 
                0x00,  // pad to offset 88 (body start)
 
                // Body: uint32_t = 7
                // We no longer serialize the body in `Serialize()`, so its not there
                // 0x07, 0x00, 0x00, 0x00,
            })));
  // clang-format on
}

TEST_F(DBusMessageTestSuite, DeserializeHelloMessageRoundTrip)
{
  // Hello message from above
  // clang-format off
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
  // clang-format on
  ASSERT_EQ(bytes.size(), 96u);

  IncomingDBusMessage message = ParseFullMessage(bytes);
  auto const& header = message.GetHeader();

  EXPECT_EQ(header.GetMessageType(), DBusMessageType::METHOD_CALL);
  EXPECT_EQ(header.GetSerial(), 1u);
  EXPECT_EQ(header.GetHeaderFieldsLength(), 78u);
  EXPECT_EQ(header.GetMessageLength(), 0u);
  EXPECT_EQ(header.GetObjectPath(), ObjectPath("/org/freedesktop/DBus"));
  EXPECT_EQ(header.GetInterface(), "org.freedesktop.DBus");
  EXPECT_EQ(header.GetMember(), "Hello");
  EXPECT_FALSE(header.GetSignature().has_value());  // no body -> no SIGNATURE field
  EXPECT_TRUE(message.GetMessageBody().empty());
}

TEST_F(DBusMessageTestSuite, DeserializeMethodReturnReplyWithBody)
{
  std::vector<byte> bytes{
      // Fixed header
      'l',
      0x02,
      0x00,
      0x01,  // endian, type=METHOD_RETURN, flags, version
      0x0A,
      0x00,
      0x00,
      0x00,  // body length = 10
      0x02,
      0x00,
      0x00,
      0x00,  // serial = 2
      0x0F,
      0x00,
      0x00,
      0x00,  // header fields length = 15

      // REPLY_SERIAL field (code 5, "u") -- starts at offset 16
      0x05,
      0x01,
      'u',
      0x00,
      0x01,
      0x00,
      0x00,
      0x00,  // reply_serial = 1

      // SIGNATURE field (code 8, "g") -- starts at offset 24 (already
      // 8-aligned, no padding needed before it)
      0x08,
      0x01,
      'g',
      0x00,
      0x01,
      's',
      0x00,  // signature length = 1, "s", NUL

      0x00,  // pad to offset 32 (body start)

      // Body: STRING ":1.42"
      0x05,
      0x00,
      0x00,
      0x00,  // string length = 5
      ':',
      '1',
      '.',
      '4',
      '2',   // ":1.42"
      0x00,  // NUL
  };
  ASSERT_EQ(bytes.size(), 42u);

  IncomingDBusMessage message = ParseFullMessage(bytes);
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
  EXPECT_EQ((std::vector<byte>(message.GetMessageBody().begin(), message.GetMessageBody().end())), expectedBody);
  EXPECT_EQ(UnmarshalDBusType<std::string>(message.GetMessageBody(), header.GetSignature()->GetSignature()), ":1.42");
}

// ---------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------

// TEST_F(DBusMessageTestSuite, ThrowsWhenFirstHeaderPartIsTooShort)
// {
//   std::vector<byte> tooShort{'l', 0x01, 0x00, 0x01};  // only 4 of 16 bytes
//   EXPECT_THROW(DBusMessageHeader{std::move(tooShort)}, DBusMalformedInputError);
// }

// TEST_F(DBusMessageTestSuite, ThrowsWhenHeaderFieldsPartDoesNotMatchDeclaredLength)
// {
//   // Fixed header declares header fields length = 78 (matching the
//   // Hello message above), but we only supply 4 bytes of header
//   // fields data.
//   std::vector<byte> firstPart{
//       'l', 0x01, 0x00, 0x01,
//       0x00, 0x00, 0x00, 0x00,
//       0x01, 0x00, 0x00, 0x00,
//   };
//   DBusMessageHeader header{std::ranges::to<std::vector>(firstPart | std::views::take(FIRST_HEADER_PART_SIZE))};
//
//   // 78 for header field length
//   header.ParseHeaderFieldLength(std::vector<byte>{0x4E, 0x00, 0x00, 0x00});
//   std::vector<byte> tooShortFields{0x01, 0x01, 'o', 0x00};
//   uint32_t arrPointer{};
//   EXPECT_THROW(header.ParseRemainderOfHeader(std::move(tooShortFields), arrPointer), DBusMalformedInputError);
//   ASSERT_EQ(header.GetHeaderFieldsLength(), 78u);
// }
// clang-format on
//
// TEST(MessageBufferTestSuite, SmallBuffersAreNotRoundedUp)
// {
//   MessageBuffer const buffer{AllocateMessageBuffer(100)};
//   EXPECT_EQ(buffer.get_deleter().capacity, 100u);
// }
//
// TEST(MessageBufferTestSuite, LargeBuffersAreRoundedUpToTheGranularity)
// {
//   // 1048700 bytes is the size of a message with a 1 MiB array
//   MessageBuffer const buffer{AllocateMessageBuffer(1048700)};
//   EXPECT_EQ(buffer.get_deleter().capacity, 17u * 64u * 1024u);
//   EXPECT_GE(buffer.get_deleter().capacity, 1048700u);
// }
//
// TEST(MessageBufferTestSuite, LargeBufferIsRecycledAfterItIsReleased)
// {
//   size_t const size{5 * 1024 * 1024 + 123};
//
//   byte* first{};
//   {
//     MessageBuffer const buffer{AllocateMessageBuffer(size)};
//     first = buffer.get();
//   }
//
//   MessageBuffer const again{AllocateMessageBuffer(size + 1000)};
//   EXPECT_EQ(again.get(), first);
//   EXPECT_GE(again.get_deleter().capacity, size + 1000);
// }
//
// TEST(MessageBufferTestSuite, BuffersTooBigForThePoolAreFreedNormally)
// {
//   size_t const size{detail::LargeBufferPool::MAX_POOLED_SIZE + 1};
//   MessageBuffer buffer{AllocateMessageBuffer(size)};
//   EXPECT_EQ(buffer.get_deleter().capacity, size);
//   buffer.reset();  // must not end up in the pool
//   EXPECT_EQ(buffer.get(), nullptr);
// }
//
// TEST(MessageBufferTestSuite, PlainUniquePtrConvertsAndIsFreedWithDeleteArray)
// {
//   std::unique_ptr<byte[]> plain{std::make_unique_for_overwrite<byte[]>(10)};
//   MessageBuffer converted{std::move(plain)};
//   EXPECT_NE(converted.get(), nullptr);
//   EXPECT_EQ(converted.get_deleter().capacity, 0u);
// }
