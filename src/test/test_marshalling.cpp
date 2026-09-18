#include <gtest/gtest.h>

#include "src/DBus.h"

using namespace cxxbus;

struct MarshalTestSuite : ::testing::Test
{
};

TEST_F(MarshalTestSuite, MarshalBoolean)
{
  // DBus BOOLEAN is wire-encoded as a 4-byte u32: 0 or 1.
  EXPECT_EQ(MarshalDBusType(true), (std::vector<byte>{0x01, 0x00, 0x00, 0x00}));
  EXPECT_EQ(MarshalDBusType(false), (std::vector<byte>{0x00, 0x00, 0x00, 0x00}));
}

TEST_F(MarshalTestSuite, MarshalIntegers)
{
  EXPECT_EQ(MarshalDBusType(static_cast<uint8_t>(0x42)), (std::vector<byte>{0x42}));
  EXPECT_EQ(MarshalDBusType(static_cast<uint8_t>(0x01)), (std::vector<byte>{0x01}));
  EXPECT_EQ(MarshalDBusType(static_cast<uint8_t>(0x00)), (std::vector<byte>{0x00}));

  EXPECT_EQ(MarshalDBusType(static_cast<int16_t>(-1)), (std::vector<byte>{0xFF, 0xFF}));
  EXPECT_EQ(MarshalDBusType(static_cast<int16_t>(0x1234)), (std::vector<byte>{0x34, 0x12}));

  EXPECT_EQ(MarshalDBusType(static_cast<uint16_t>(0x1234)), (std::vector<byte>{0x34, 0x12}));
  EXPECT_EQ(MarshalDBusType(static_cast<uint16_t>(0)), (std::vector<byte>{0x00, 0x00}));

  EXPECT_EQ(MarshalDBusType(static_cast<int32_t>(-2)), (std::vector<byte>{0xFE, 0xFF, 0xFF, 0xFF}));
  EXPECT_EQ(MarshalDBusType(static_cast<int32_t>(0)), (std::vector<byte>{0x00, 0x00, 0x00, 0x00}));
  EXPECT_EQ(MarshalDBusType(static_cast<int32_t>(0x1234)), (std::vector<byte>{0x34, 0x12, 0x00, 0x00}));
  EXPECT_EQ(MarshalDBusType(std::numeric_limits<int32_t>::min()), (std::vector<byte>{0x00, 0x00, 0x00, 0x80}));
  EXPECT_EQ(MarshalDBusType(std::numeric_limits<int32_t>::max()), (std::vector<byte>{0xFF, 0xFF, 0xFF, 0x7F}));

  EXPECT_EQ(MarshalDBusType(static_cast<uint32_t>(42)), (std::vector<byte>{0x2A, 0x00, 0x00, 0x00}));
  EXPECT_EQ(MarshalDBusType(static_cast<uint32_t>(0)), (std::vector<byte>{0x00, 0x00, 0x00, 0x00}));
  EXPECT_EQ(MarshalDBusType(static_cast<uint32_t>(0xDEADBEEF)), (std::vector<byte>{0xEF, 0xBE, 0xAD, 0xDE}));

  EXPECT_EQ(MarshalDBusType(static_cast<int64_t>(-1)),
            (std::vector<byte>{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));

  EXPECT_EQ(MarshalDBusType(static_cast<uint64_t>(0x0102030405060708ULL)),
            (std::vector<byte>{0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01}));
  EXPECT_EQ(MarshalDBusType(std::numeric_limits<uint64_t>::max()),
            (std::vector<byte>{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
}

TEST_F(MarshalTestSuite, MarshalDouble)
{
  EXPECT_EQ(MarshalDBusType(1.0), (std::vector<byte>{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F}));
  EXPECT_EQ(MarshalDBusType(0.0), (std::vector<byte>{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));
  EXPECT_EQ(MarshalDBusType(-2.5), (std::vector<byte>{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xC0}));
  EXPECT_EQ(MarshalDBusType(std::numeric_limits<double>::infinity()),
            (std::vector<byte>{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x7F}));
  EXPECT_EQ(MarshalDBusType(-std::numeric_limits<double>::infinity()),
            (std::vector<byte>{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0xFF}));
  EXPECT_EQ(MarshalDBusType(std::numeric_limits<double>::quiet_NaN()),
            (std::vector<byte>{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x7F}));
}

TEST_F(MarshalTestSuite, MarshalStrings)
{
  EXPECT_EQ(MarshalDBusType(std::string("")), (std::vector<byte>{0x00, 0x00, 0x00, 0x00,     // length = 0
                                                                 0x00}));                    // null terminator
  EXPECT_EQ(MarshalDBusType(std::string("Hi!")), (std::vector<byte>{0x03, 0x00, 0x00, 0x00,  // length = 3
                                                                    'H', 'i', '!',           // string
                                                                    0x00}));                 // null terminator

  EXPECT_EQ(MarshalDBusType("Hi!"), (std::vector<byte>{0x03, 0x00, 0x00, 0x00, 'H', 'i', '!', 0x00}));
}

TEST_F(MarshalTestSuite, MarshalBigString)
{
  std::string str{};
  for (int i{}; i < 10'000; ++i)
  {
    str.push_back(std::max(i % 127, 1));
  }

  std::vector<byte> expectedData{
      0x10, 0x27, 0x00, 0x00  // Length = 10'000 = 0x2710
  };
  expectedData.append_range(str);
  expectedData.push_back('\0');

  EXPECT_EQ(MarshalDBusType(str), expectedData);
}

TEST_F(MarshalTestSuite, MarshalStringContainingNullCharacter)
{
  EXPECT_THROW(MarshalDBusType(std::string{"Hello\0World", 11}), DBusSerializationError);
}

TEST_F(MarshalTestSuite, MarshalObjectPath)
{
  std::string const path = "/org/example/Foo";
  std::vector<byte> expected = {static_cast<byte>(path.size()), 0x00, 0x00, 0x00};
  expected.insert(expected.end(), path.begin(), path.end());
  expected.push_back(0x00);

  EXPECT_EQ(MarshalDBusType(ObjectPath(path)), expected);
}

TEST_F(MarshalTestSuite, MarshalStringWithMultiByteUtf8Character)
{
  // "é" is U+00E9, encoded in UTF-8 as the 2 bytes 0xC3 0xA9.
  std::string const value = "\xC3\xA9";                                         // "é"
  EXPECT_EQ(MarshalDBusType(value), (std::vector<byte>{0x02, 0x00, 0x00, 0x00,  // length = 2 (bytes, not chars)
                                                       0xC3, 0xA9,              // UTF-8 bytes
                                                       0x00}));                 // null terminator
}

TEST_F(MarshalTestSuite, MarshalSignature)
{
  EXPECT_EQ(MarshalDBusType(Signature("ai")),
            (std::vector<byte>{0x02,      // length byte
                               'a', 'i',  // signature chars
                               0x00}));   // null terminator

  EXPECT_EQ(MarshalDBusType(Signature("")), (std::vector<byte>{0x00, 0x00}));  // len=0, NUL
}

TEST_F(MarshalTestSuite, MarshalArray)
{
  // Empty array still needs a u32 length field
  EXPECT_EQ(MarshalDBusType(std::vector<uint32_t>{}), (std::vector<byte>{0x00, 0x00, 0x00, 0x00}));

  EXPECT_EQ(MarshalDBusType(std::vector<uint32_t>{1, 2, 3}),
            (std::vector<byte>{0x0C, 0x00, 0x00, 0x00,  // byte length = 12 (3 * 4)
                               0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00}));

  EXPECT_EQ(MarshalDBusType(std::array<uint32_t, 3>{1, 2, 3}),
            (std::vector<byte>{0x0C, 0x00, 0x00, 0x00,  // byte length = 12 (3 * 4)
                               0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00}));

  EXPECT_EQ(MarshalDBusType(std::vector<uint64_t>{1, 2, 3}),
            (std::vector<byte>{0x18, 0x00, 0x00, 0x00,  // byte length = 24 (3 * 8)
                               0x00, 0x00, 0x00, 0x00,  // Pad to 8 byte boundary
                               0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));

  EXPECT_EQ(MarshalDBusType(std::vector<std::string>{"a", "bc"}),
            (std::vector<byte>{
                0x0F, 0x00, 0x00, 0x00,                   // byte length = 15
                0x01, 0x00, 0x00, 0x00, 'a', 0x00,        // "a"  (6 bytes)
                0x00, 0x00,                               // padding (2 bytes)
                0x02, 0x00, 0x00, 0x00, 'b', 'c',  0x00,  // "bc" (7 bytes)
            }));

  EXPECT_EQ(MarshalDBusType(std::array<std::string, 2>{"a", "bc"}),
            (std::vector<byte>{
                0x0F, 0x00, 0x00, 0x00,                   // byte length = 15
                0x01, 0x00, 0x00, 0x00, 'a', 0x00,        // "a"  (6 bytes)
                0x00, 0x00,                               // padding (2 bytes)
                0x02, 0x00, 0x00, 0x00, 'b', 'c',  0x00,  // "bc" (7 bytes)
            }));

  // clang-format off
  EXPECT_EQ(MarshalDBusType(std::vector<std::string>{"", ""}), (std::vector<byte>{
                                                                   0x0D, 0x00, 0x00, 0x00,  // array length = 13
                                                                   0x00, 0x00, 0x00, 0x00, 0x00,  // "" (5 bytes: u32 + null)
                                                                   0x00, 0x00, 0x00,  // padding (3 bytes)
                                                                   0x00, 0x00, 0x00, 0x00, 0x00,  // "" (5 bytes: u32 + null)
                                                               }));

  EXPECT_EQ(MarshalDBusType(std::vector<bool>{true, false, true}), (std::vector<byte>{
                                                                       0x0C, 0x00, 0x00, 0x00,  // array length = 12 (3 * 4)
                                                                       0x01, 0x00, 0x00, 0x00, // true
                                                                       0x00, 0x00, 0x00, 0x00, // false
                                                                       0x01, 0x00, 0x00, 0x00, // true
                                                                   }));
  // clang-format on
}

TEST_F(MarshalTestSuite, MarshalStruct)
{
  EXPECT_EQ(MarshalDBusType(std::make_tuple(static_cast<uint8_t>(0x01), static_cast<uint32_t>(0x11223344))),
            (std::vector<byte>{0x01,                       // uint8_t
                               0x00, 0x00, 0x00,           // padding
                               0x44, 0x33, 0x22, 0x11}));  // uint32

  // Nested structs, structs (not the struct fields!) are ALWAYS aligned to an 8-byte boundary
  EXPECT_EQ(
      MarshalDBusType(std::make_tuple(static_cast<uint8_t>(0x01), std::make_tuple(static_cast<uint16_t>(0x0203)))),
      (std::vector<byte>{0x01,                                      // outer byte
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // pad to offset 8 for nested struct
                         0x03, 0x02}));                             // inner uint16

  EXPECT_EQ(MarshalDBusType(std::make_tuple(static_cast<uint16_t>(0x0102), static_cast<uint64_t>(1))),
            (std::vector<byte>{0x02, 0x01,                                         // uint16
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                 // pad to 8-byte boundary for u64
                               0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));  // u64
}

TEST_F(MarshalTestSuite, MarshalMap)
{
  // Even an empty map still needs to be padded to 8 byte boundary
  EXPECT_EQ(MarshalDBusType(std::map<uint32_t, uint32_t>{}),
            (std::vector<byte>{0x00, 0x00, 0x00, 0x00,     // array length = 0
                               0x00, 0x00, 0x00, 0x00}));  // pad to 8-byte boundary

  EXPECT_EQ(MarshalDBusType(std::map<uint32_t, uint32_t>{{1, 10}, {2, 20}}),
            (std::vector<byte>{
                0x10, 0x00, 0x00, 0x00,  // array length = 16 (2 entries * 8 bytes)
                0x00, 0x00, 0x00, 0x00,  // pad to 8-byte boundary for first entry
                0x01, 0x00, 0x00, 0x00,  // entry 1 key = 1
                0x0A, 0x00, 0x00, 0x00,  // entry 1 value = 10
                0x02, 0x00, 0x00, 0x00,  // entry 2 key = 2
                0x14, 0x00, 0x00, 0x00,  // entry 2 value = 20
            }));

  EXPECT_EQ(MarshalDBusType(std::map<uint32_t, uint16_t>{{1, 0xBEEF}, {2, 0xDEAD}}),
            (std::vector<byte>{
                0x0E, 0x00, 0x00, 0x00,  // array length = 14
                0x00, 0x00, 0x00, 0x00,  // pad to 8-byte boundary for first entry
                0x01, 0x00, 0x00, 0x00,  // entry 1 key = 1
                0xEF, 0xBE,              // entry 1 value = 0xBEEF
                0x00, 0x00,              // add 2 padding bytes to get next entry to 8-byte offset
                0x02, 0x00, 0x00, 0x00,  // entry 2 key = 2
                0xAD, 0xDE,              // entry 2 value = 0xDEAD
            }));

  EXPECT_EQ(MarshalDBusType(std::map<std::string, uint32_t>{{"a", 1}}),
            (std::vector<byte>{
                0x0C, 0x00, 0x00, 0x00,             // array length = 12
                0x00, 0x00, 0x00, 0x00,             // pad to 8-byte boundary for the entry
                0x01, 0x00, 0x00, 0x00, 'a', 0x00,  // key "a" (6 bytes)
                0x00, 0x00,                         // pad value to 4-byte boundary for u32
                0x01, 0x00, 0x00, 0x00,             // value = 1
            }));
}

TEST_F(MarshalTestSuite, MarshalVariant)
{
  EXPECT_EQ(MarshalDBusType(Variant::Create(static_cast<uint8_t>(0x07))),
            (std::vector<byte>{0x01, 'y', 0x00,  // signature "y"
                               0x07}));          // value

  EXPECT_EQ(MarshalDBusType(Variant::Create(static_cast<uint32_t>(42))),
            (std::vector<byte>{0x01, 'u', 0x00,            // signature "u"
                               0x00,                       // pad to 4-byte boundary for u32
                               0x2A, 0x00, 0x00, 0x00}));  // value = 42

  // clang-format off
  EXPECT_EQ(MarshalDBusType(Variant::Create(2.5)), (std::vector<byte>{
                                               0x01, 'd', 0x00,  // signature "d"
                                               0x00, 0x00, 0x00, 0x00, 0x00,  // pad to 8-byte boundary
                                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x40,  // value = 2.5
                                           }));

  EXPECT_EQ(MarshalDBusType(Variant::Create(std::string("Hi!"))), (std::vector<byte>{
                                                              0x01, 's', 0x00,  // signature "s"
                                                              0x00,  // pad to 4-byte boundary
                                                              0x03, 0x00, 0x00, 0x00,  // string length = 3
                                                              'H', 'i', '!', 0x00,  // string + null terminator
                                                          }));

  EXPECT_EQ(MarshalDBusType(Variant::Create("Hi!")), (std::vector<byte>{
                                                              0x01, 's', 0x00,  // signature "s"
                                                              0x00,  // pad to 4-byte boundary
                                                              0x03, 0x00, 0x00, 0x00,  // string length = 3
                                                              'H', 'i', '!', 0x00,  // string + null terminator
                                                          }));

  EXPECT_EQ(MarshalDBusType(Variant::Create(std::string(""))), (std::vector<byte>{
                                                           0x01, 's', 0x00,  // signature "s"
                                                           0x00,  // pad to 4-byte boundary
                                                           0x00, 0x00, 0x00, 0x00,  // string length = 0
                                                           0x00,  // null terminator
                                                       }));

  EXPECT_EQ(MarshalDBusType(Variant::Create(std::vector<uint32_t>{1, 2})), (std::vector<byte>{
                                                                       0x02, 'a', 'u', 0x00,  // signature "au"
                                                                       0x08, 0x00, 0x00, 0x00,  // array length = 8
                                                                       0x01, 0x00, 0x00, 0x00, // element 1
                                                                       0x02, 0x00, 0x00, 0x00, // element 2
                                                                   }));

  EXPECT_EQ(MarshalDBusType(Variant::Create(std::make_tuple(static_cast<uint8_t>(0x01), static_cast<uint32_t>(0x11223344)))), (std::vector<byte>{
                      0x04, '(', 'y', 'u', ')', 0x00,  // signature "(yu)"
                      0x00, 0x00,  // pad to 8-byte boundary
                      0x01, // first struct field: u8
                      0x00, 0x00, 0x00,  // pad to 4-byte boundary
                      0x44, 0x33, 0x22, 0x11,  // struct u32
                  }));

  EXPECT_EQ(MarshalDBusType(Variant::Create(Variant::Create(static_cast<uint8_t>(0x07)))), (std::vector<byte>{
                                                                                         0x01, 'v', 0x00,  // outer signature "v"
                                                                                         0x01, 'y', 0x00,  // inner signature "y"
                                                                                         0x07,  // inner u8
                                                                                     }));

  EXPECT_EQ(MarshalDBusType(std::make_tuple(static_cast<uint8_t>(0xAA), static_cast<uint8_t>(0xBB),
                                              Variant::Create(static_cast<uint32_t>(0x11223344)))),
                                               (std::vector<byte>{
                                                   0xAA,  // first struct field: u8
                                                   0xBB,  // second struct field: u8
                                                   // Variant requires no padding
                                                   0x01, 'u', 0x00,  // variant signature "u"
                                                   0x00, 0x00, 0x00,  // pad to 4-byte boundary
                                                   0x44, 0x33, 0x22, 0x11,  // variant's uint32_t value
                                               }));
  // clang-format on
}

TEST_F(MarshalTestSuite, MarshalMultipleCompleteTypes)
{
  EXPECT_EQ(MarshalDBusType(MultipleCompleteTypes<uint8_t, uint32_t>(static_cast<uint8_t>(0x01),
                                                                     static_cast<uint32_t>(0x11223344))),
            (std::vector<byte>{0x01,                       // u8
                               0x00, 0x00, 0x00,           // pad to 4-byte boundary
                               0x44, 0x33, 0x22, 0x11}));  // uint32

  EXPECT_EQ(MarshalDBusType(MultipleCompleteTypes<int32_t, int32_t>(10, -10)),
            (std::vector<byte>{0x0A, 0x00, 0x00, 0x00,     // first int32 = 10
                               0xF6, 0xFF, 0xFF, 0xFF}));  // second int32 = -10

  // clang-format off
  EXPECT_EQ(MarshalDBusType(MultipleCompleteTypes<uint8_t, std::string>(static_cast<uint8_t>(0xAA), "Hi!")),
            (std::vector<byte>{
                0xAA, // u8
                0x00, 0x00, 0x00,  // Pad to 4-byte boundary
                0x03, 0x00, 0x00, 0x00,  // string length = 3
                'H', 'i', '!', 0x00,  // string + null terminator
            }));
  // clang-format on
}

TEST_F(MarshalTestSuite, MarshalVeryNestedMaps)
{
  // map<uint32_t, map<uint32_t, map<uint32_t, uint32_t>>> with one
  // entry at each level: outer key=1 -> {2 -> {3 -> 30}}.
  //
  // offset 0-3:   outer array length (filled in below)
  // offset 4-7:   pad to 8 for the outer DICT_ENTRY's start
  // offset 8-11:  outer key = 1
  // offset 12-15: middle array length (value of outer entry; ARRAY
  //               needs 4-byte alignment, and offset 12 is already
  //               4-aligned, so no extra padding here)
  // offset 16-19: middle key = 2 (offset 16 is already 8-aligned, so
  //               no padding needed before the middle DICT_ENTRY)
  // offset 20-23: inner array length (value of middle entry; offset
  //               20 is already 4-aligned)
  // offset 24-27: inner key = 3 (offset 24 is already 8-aligned)
  // offset 28-31: inner value = 30
  using InnerMap = std::map<uint32_t, uint32_t>;
  using MiddleMap = std::map<uint32_t, InnerMap>;
  using OuterMap = std::map<uint32_t, MiddleMap>;

  OuterMap map{{1, MiddleMap{{2, InnerMap{{3, 30}}}}}};

  EXPECT_EQ(MarshalDBusType(map), (std::vector<byte>{
                                      0x18, 0x00, 0x00, 0x00,  // outer array length = 24
                                      0x00, 0x00, 0x00, 0x00,  // pad to 8 for outer entry
                                      0x01, 0x00, 0x00, 0x00,  // outer key = 1
                                      0x10, 0x00, 0x00, 0x00,  // middle array length = 16
                                      0x02, 0x00, 0x00, 0x00,  // middle key = 2
                                      0x08, 0x00, 0x00, 0x00,  // inner array length = 8
                                      0x03, 0x00, 0x00, 0x00,  // inner key = 3
                                      0x1E, 0x00, 0x00, 0x00,  // inner value = 30
                                  }));
}

TEST_F(MarshalTestSuite, MarshalVeryNestedStructs)
{
  // tuple<byte, tuple<byte, tuple<byte, uint32_t>>>: each nested
  // struct forces the *outer* struct back onto an 8-byte boundary
  // before it can start, even though the leading field of each level
  // is a single byte that would only need 1-byte alignment on its own.
  //
  // offset 0:     outermost byte = 0xAA
  // offset 1-7:   pad to 8 for the middle struct's start
  // offset 8:     middle byte = 0xBB
  // offset 9-15:  pad to 16 for the innermost struct's start
  // offset 16:    innermost byte = 0xCC
  // offset 17-19: pad to 20 for the uint32_t field's 4-byte alignment
  // offset 20-23: uint32_t value = 0x11223344
  auto data = MarshalDBusType(
      std::make_tuple(static_cast<uint8_t>(0xAA),
                      std::make_tuple(static_cast<uint8_t>(0xBB),
                                      std::make_tuple(static_cast<uint8_t>(0xCC), static_cast<uint32_t>(0x11223344)))));

  EXPECT_EQ(data,
            (std::vector<byte>{
                0xAA,                                      // outermost byte
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // pad to offset 8
                0xBB,                                      // middle byte
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // pad to offset 16
                0xCC,                                      // innermost byte
                0x00, 0x00, 0x00,                          // pad to offset 20
                0x44, 0x33, 0x22, 0x11,                    // uint32_t value
            }));
}

TEST_F(MarshalTestSuite, MarshalVeryNestedArrayStructs)
{
  std::vector<std::tuple<std::vector<std::tuple<std::vector<std::tuple<int, int, int>>>>>> vec{

      std::tuple<std::vector<std::tuple<std::vector<std::tuple<int, int, int>>>>>{

          std::vector<std::tuple<std::vector<std::tuple<int, int, int>>>>{

              std::tuple<std::vector<std::tuple<int, int, int>>>{

                  std::vector<std::tuple<int, int, int>>{
                      std::tuple<int, int, int>{1, 2, 3},
                      std::tuple<int, int, int>{42, 43, 44}}  // std::vector<std::tuple<int, int, int>>

              }  // std::tuple<std::vector<std::tuple<int, int, int>>>

          }  // std::vector<std::tuple<std::vector<std::tuple<int, int, int>>>>

      }  // std::tuple<std::vector<std::tuple<std::vector<std::tuple<int, int, int>>>>>

  };  // vec

  std::vector<byte> bytes = {
      // First Vector
      0x2C,
      0x00,
      0x00,
      0x00,  // Length of vector (44 bytes)

      // First Struct
      0x00,
      0x00,
      0x00,
      0x00,  // Padding to 8 byte boundary (tuple)

      // Second Vector
      0x24,
      0x00,
      0x00,
      0x00,  // Length of vector (36 bytes)

      0x00,
      0x00,
      0x00,
      0x00,  // Padding to 8 byte boundary

      // Second Struct and Third (And final) Vector
      0x1C,
      0x00,
      0x00,
      0x00,  // Length of vector (28 bytes)

      0x00,
      0x00,
      0x00,
      0x00,  // Padding to 8 byte boundary

      // Finally, some actual data, our tuple of integers
      0x01,
      0x00,
      0x00,
      0x00,  // First integer (1)
      0x02,
      0x00,
      0x00,
      0x00,  // Second integer (2)
      0x03,
      0x00,
      0x00,
      0x00,  // Third integer (3)

      0x00,
      0x00,
      0x00,
      0x00,  // Padding to 8 byte boundary

      // Our 2nd tuple of integers.
      0x2A,
      0x00,
      0x00,
      0x00,  // First integer (42)
      0x2B,
      0x00,
      0x00,
      0x00,  // First integer (43)
      0x2C,
      0x00,
      0x00,
      0x00,  // First integer (44)
  };

  EXPECT_EQ(MarshalDBusType(vec), bytes);
}

TEST_F(MarshalTestSuite, MarshalStructContainingMapContainingStructContainingMap)
{
  // tuple<byte, map<uint32_t, tuple<byte, map<uint32_t, uint32_t>>>>
  //
  // Struct: (byte=0xAA, MapA{1 -> InnerStruct})
  // InnerStruct: (byte=0xBB, MapB{2 -> 20})
  //
  // offset 0:     outer struct's byte field = 0xAA
  // offset 1-3:   pad to 4 for MapA's array-length field
  // offset 4-7:   MapA's array length (filled in below)
  // offset 8-11:  MapA entry key = 1 (offset 8 is already 8-aligned,
  //               so no padding before this DICT_ENTRY)
  // offset 12-15: pad to 16 for InnerStruct's 8-byte-aligned start
  //               (this is the DICT_ENTRY's value field; STRUCT
  //               requires 8-byte alignment regardless of what
  //               container it's sitting in)
  // offset 16:    InnerStruct's byte field = 0xBB
  // offset 17-19: pad to 20 for MapB's array-length field
  // offset 20-23: MapB's array length (filled in below)
  // offset 24-27: MapB entry key = 2 (offset 24 is already 8-aligned)
  // offset 28-31: MapB entry value = 20
  using MapB = std::map<uint32_t, uint32_t>;
  using InnerStruct = std::tuple<uint8_t, MapB>;
  using MapA = std::map<uint32_t, InnerStruct>;

  auto data = MarshalDBusType(
      std::make_tuple(static_cast<uint8_t>(0xAA), MapA{{1, InnerStruct{static_cast<uint8_t>(0xBB), MapB{{2, 20}}}}}));

  EXPECT_EQ(data, (std::vector<byte>{
                      0xAA, 0x00, 0x00, 0x00,  // outer byte + pad to 4 for map (Array)
                      0x18, 0x00, 0x00, 0x00,  // MapA array length = 24
                      0x01, 0x00, 0x00, 0x00,  // MapA entry key = 1
                      0x00, 0x00, 0x00, 0x00,  // pad to 16 for InnerStruct
                      0xBB, 0x00, 0x00, 0x00,  // InnerStruct byte + pad to 4 for map (Array)
                      0x08, 0x00, 0x00, 0x00,  // MapB array length = 8
                      0x02, 0x00, 0x00, 0x00,  // MapB entry key = 2
                      0x14, 0x00, 0x00, 0x00,  // MapB entry value = 20
                  }));
}

TEST_F(MarshalTestSuite, MarshalsIndependentValuesEachStartFromOffsetZero)
{
  // Since each MarshalDBusType() call produces its own standalone
  // vector, two calls back-to-back should NOT show cross-call
  // alignment padding -- each starts fresh.
  auto first = MarshalDBusType(static_cast<uint8_t>(0xAA));
  auto second = MarshalDBusType(static_cast<uint32_t>(1));

  EXPECT_EQ(first, (std::vector<byte>{0xAA}));
  EXPECT_EQ(second, (std::vector<byte>{0x01, 0x00, 0x00, 0x00}));
}
// clang-format on
