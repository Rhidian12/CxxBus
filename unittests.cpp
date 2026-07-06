#include <gtest/gtest.h>

#include <cstring>
#include <iostream>

#include "DBus.h"

struct MarshalTestSuite : ::testing::Test
{
};

// clang-format off

// ---------------------------------------------------------------------
// Fixed-size basic types
// ---------------------------------------------------------------------
 
TEST_F(MarshalTestSuite, MarshalByte)
{
  EXPECT_EQ(MarshalDBusType(static_cast<uint8_t>(0x42)), (std::vector<byte>{0x42}));
}
 
TEST_F(MarshalTestSuite, MarshalBooleanTrue)
{
  // DBus BOOLEAN is wire-encoded as a 4-byte UINT32: 0 or 1.
  EXPECT_EQ(MarshalDBusType(true), (std::vector<byte>{0x01, 0x00, 0x00, 0x00}));
}
 
TEST_F(MarshalTestSuite, MarshalBooleanFalse)
{
  EXPECT_EQ(MarshalDBusType(false), (std::vector<byte>{0x00, 0x00, 0x00, 0x00}));
}
 
TEST_F(MarshalTestSuite, MarshalInt16)
{
  EXPECT_EQ(MarshalDBusType(static_cast<int16_t>(-1)), (std::vector<byte>{0xFF, 0xFF}));
}
 
TEST_F(MarshalTestSuite, MarshalUint16)
{
  EXPECT_EQ(MarshalDBusType(static_cast<uint16_t>(0x1234)), (std::vector<byte>{0x34, 0x12}));
}
 
TEST_F(MarshalTestSuite, MarshalInt32)
{
  EXPECT_EQ(MarshalDBusType(static_cast<int32_t>(-2)), (std::vector<byte>{0xFE, 0xFF, 0xFF, 0xFF}));
}
 
TEST_F(MarshalTestSuite, MarshalUint32)
{
  EXPECT_EQ(MarshalDBusType(static_cast<uint32_t>(42)), (std::vector<byte>{0b101010, 0b0000, 0b0000, 0b0000}));
}
 
TEST_F(MarshalTestSuite, MarshalUint32MaxValue)
{
  EXPECT_EQ(MarshalDBusType(static_cast<uint32_t>(0xDEADBEEF)),
            (std::vector<byte>{0xEF, 0xBE, 0xAD, 0xDE}));
}
 
TEST_F(MarshalTestSuite, MarshalInt64)
{
  EXPECT_EQ(MarshalDBusType(static_cast<int64_t>(-1)),
            (std::vector<byte>{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
}
 
TEST_F(MarshalTestSuite, MarshalUint64)
{
  EXPECT_EQ(MarshalDBusType(static_cast<uint64_t>(0x0102030405060708ULL)),
            (std::vector<byte>{0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01}));
}
 
TEST_F(MarshalTestSuite, MarshalDouble)
{
  // IEEE-754 double for 1.0 is 0x3FF0000000000000, little-endian bytes.
  EXPECT_EQ(MarshalDBusType(1.0),
            (std::vector<byte>{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F}));
}
 
TEST_F(MarshalTestSuite, MarshalUint32ZeroValue)
{
  EXPECT_EQ(MarshalDBusType(static_cast<uint32_t>(0)),
            (std::vector<byte>{0x00, 0x00, 0x00, 0x00}));
}
 
TEST_F(MarshalTestSuite, MarshalInt32MinValue)
{
  // INT32_MIN = -2147483648 = 0x80000000
  EXPECT_EQ(MarshalDBusType(std::numeric_limits<int32_t>::min()),
            (std::vector<byte>{0x00, 0x00, 0x00, 0x80}));
}
 
TEST_F(MarshalTestSuite, MarshalInt32MaxValue)
{
  // INT32_MAX = 2147483647 = 0x7FFFFFFF
  EXPECT_EQ(MarshalDBusType(std::numeric_limits<int32_t>::max()),
            (std::vector<byte>{0xFF, 0xFF, 0xFF, 0x7F}));
}
 
TEST_F(MarshalTestSuite, MarshalUint64MaxValue)
{
  EXPECT_EQ(MarshalDBusType(std::numeric_limits<uint64_t>::max()),
            (std::vector<byte>{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
}
 
TEST_F(MarshalTestSuite, MarshalDoubleZero)
{
  EXPECT_EQ(MarshalDBusType(0.0),
            (std::vector<byte>{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));
}
 
TEST_F(MarshalTestSuite, MarshalDoubleNegative)
{
  // IEEE-754 double for -2.5 is 0xC004000000000000.
  EXPECT_EQ(MarshalDBusType(-2.5),
            (std::vector<byte>{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xC0}));
}
 
// ---------------------------------------------------------------------
// STRING / OBJECT_PATH: UINT32 length + UTF-8 bytes + NUL terminator
// ---------------------------------------------------------------------
 
TEST_F(MarshalTestSuite, MarshalEmptyString)
{
  EXPECT_EQ(MarshalDBusType(std::string("")),
            (std::vector<byte>{0x00, 0x00, 0x00, 0x00,  // length = 0
                                0x00}));                 // NUL terminator
}
 
TEST_F(MarshalTestSuite, MarshalString)
{
  EXPECT_EQ(MarshalDBusType(std::string("Hi!")),
            (std::vector<byte>{0x03, 0x00, 0x00, 0x00,  // length = 3
                                'H', 'i', '!',           // UTF-8 bytes
                                0x00}));                 // NUL terminator
}
 
TEST_F(MarshalTestSuite, StringLengthExcludesNulTerminator)
{
  auto data = MarshalDBusType(std::string("abcd"));
  uint32_t length;
  std::memcpy(&length, data.data(), sizeof(length));
  EXPECT_EQ(length, 4u);              // NOT 5 -- NUL isn't counted.
  EXPECT_EQ(data.size(), 4 + 4 + 1u); // len field + chars + NUL
  EXPECT_EQ(data.back(), 0x00);
}
 
TEST_F(MarshalTestSuite, MarshalObjectPath)
{
  const std::string path = "/org/example/Foo";
  std::vector<byte> expected = {static_cast<byte>(path.size()), 0x00, 0x00, 0x00};
  expected.insert(expected.end(), path.begin(), path.end());
  expected.push_back(0x00);
 
  EXPECT_EQ(MarshalDBusType(ObjectPath(path)), expected);
}
 
TEST_F(MarshalTestSuite, MarshalStringWithMultiByteUtf8Character)
{
  // "é" is U+00E9, encoded in UTF-8 as the 2 bytes 0xC3 0xA9. The
  // STRING length field counts *bytes*, not Unicode code points --
  // this is a single character but a length of 2, not 1.
  const std::string value = "\xC3\xA9";  // "é"
  EXPECT_EQ(MarshalDBusType(value),
            (std::vector<byte>{0x02, 0x00, 0x00, 0x00,  // length = 2 (bytes, not chars)
                                0xC3, 0xA9,               // UTF-8 bytes
                                0x00}));                  // NUL terminator
}
 
TEST_F(MarshalTestSuite, MarshalObjectPathRoot)
{
  // "/" is the only object path allowed to consist of just the root
  // separator with no trailing segment.
  EXPECT_EQ(MarshalDBusType(ObjectPath("/")),
            (std::vector<byte>{0x01, 0x00, 0x00, 0x00,  // length = 1
                                '/',                      // path
                                0x00}));                  // NUL terminator
}
 
// ---------------------------------------------------------------------
// SIGNATURE: single BYTE length + bytes + NUL terminator
// ---------------------------------------------------------------------
 
TEST_F(MarshalTestSuite, MarshalSignature)
{
  EXPECT_EQ(MarshalDBusType(Signature("ai")),
            (std::vector<byte>{0x02,        // length byte (not UINT32!)
                                'a', 'i',    // signature chars
                                0x00}));     // NUL terminator
}
 
TEST_F(MarshalTestSuite, MarshalEmptySignature)
{
  EXPECT_EQ(MarshalDBusType(Signature("")), (std::vector<byte>{0x00, 0x00}));  // len=0, NUL
}
 
// ---------------------------------------------------------------------
// ARRAY: UINT32 byte-length prefix + elements
// ---------------------------------------------------------------------
 
TEST_F(MarshalTestSuite, MarshalEmptyArrayOfUint32)
{
  EXPECT_EQ(MarshalDBusType(std::vector<uint32_t>{}), (std::vector<byte>{0x00, 0x00, 0x00, 0x00}));
}
 
TEST_F(MarshalTestSuite, MarshalArrayOfUint32)
{
  EXPECT_EQ(MarshalDBusType(std::vector<uint32_t>{1, 2, 3}),
            (std::vector<byte>{0x0C, 0x00, 0x00, 0x00,  // byte length = 12 (3 * 4)
                                0x01, 0x00, 0x00, 0x00,
                                0x02, 0x00, 0x00, 0x00,
                                0x03, 0x00, 0x00, 0x00}));
}
 
TEST_F(MarshalTestSuite, MarshalArrayOfStrings)
{
  // Each STRING element's UINT32 length field must be 4-byte aligned.
  // "a" occupies 6 bytes (4 len + 1 char + 1 NUL), landing the next
  // element at a non-4-aligned offset, so 2 padding bytes are inserted
  // before "bc" -- and those padding bytes count toward the array's
  // byte length.
  EXPECT_EQ(MarshalDBusType(std::vector<std::string>{"a", "bc"}),
            (std::vector<byte>{
                0x0F, 0x00, 0x00, 0x00,                    // byte length = 15
                0x01, 0x00, 0x00, 0x00, 'a', 0x00,          // "a"  (6 bytes)
                0x00, 0x00,                                 // padding (2 bytes)
                0x02, 0x00, 0x00, 0x00, 'b', 'c', 0x00,     // "bc" (7 bytes)
            }));
}
 
TEST_F(MarshalTestSuite, ArrayElementPaddingIsCountedInArrayLength)
{
  // Isolates the padding behavior: two single-character strings still
  // require 2 padding bytes between them, since each occupies 6 bytes
  // (4 + 1 + 1), landing the next element's length field at offset 6
  // relative to the start of the array content -- not 4-aligned.
  auto data = MarshalDBusType(std::vector<std::string>{"x", "y"});
  uint32_t array_length;
  std::memcpy(&array_length, data.data(), sizeof(array_length));
  EXPECT_EQ(array_length, 6u + 2u + 6u);  // "x" + pad + "y" = 14
  EXPECT_EQ(data.size(), 4u + array_length);
}
 
TEST_F(MarshalTestSuite, MarshalArrayOfEmptyStrings)
{
  // Even "empty" elements still occupy space and need alignment
  // tracking: an empty string is 5 bytes (4-byte length of 0 + a
  // single NUL, no content bytes). That's not a multiple of 4, so 3
  // padding bytes are needed before the second element's length field.
  EXPECT_EQ(MarshalDBusType(std::vector<std::string>{"", ""}),
            (std::vector<byte>{
                0x0D, 0x00, 0x00, 0x00,        // array length = 13
                0x00, 0x00, 0x00, 0x00, 0x00,  // "" (5 bytes: len=0 + NUL)
                0x00, 0x00, 0x00,              // padding (3 bytes)
                0x00, 0x00, 0x00, 0x00, 0x00,  // "" (5 bytes)
            }));
}
 
TEST_F(MarshalTestSuite, MarshalArrayOfBooleans)
{
  // BOOLEAN is wire-encoded as a 4-byte UINT32, so an array of
  // booleans has no inter-element padding -- every element is already
  // a multiple of its own alignment requirement.
  //
  // NOTE: std::vector<bool> is a bit-packed specialization with proxy
  // references rather than real bool elements/contiguous storage. If
  // your MarshalDBusType(std::vector<T>) overload assumes a
  // contiguous, dereferenceable element type, this specific
  // instantiation is worth testing explicitly -- it's a classic trap.
  EXPECT_EQ(MarshalDBusType(std::vector<bool>{true, false, true}),
            (std::vector<byte>{
                0x0C, 0x00, 0x00, 0x00,  // array length = 12 (3 * 4)
                0x01, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
                0x01, 0x00, 0x00, 0x00,
            }));
}
 
// ---------------------------------------------------------------------
// STRUCT: fields packed with alignment padding between them
// ---------------------------------------------------------------------
 
TEST_F(MarshalTestSuite, MarshalStruct)
{
  // NOTE: this does NOT exercise the struct's own 8-byte alignment
  // requirement. That requirement is about the struct's *starting*
  // offset within its enclosing buffer; since MarshalDBusType starts
  // this call fresh at offset 0 (trivially a multiple of 8), no
  // leading padding is visible here. The 3 padding bytes below come
  // from the UINT32 *field's* own 4-byte alignment, not from the
  // struct being 8-byte aligned. See MarshalNestedStructAlignsToEightBytes
  // below for a test that actually forces struct-start padding.
  auto data = MarshalDBusType(std::make_tuple(static_cast<uint8_t>(0x01),
                                               static_cast<uint32_t>(0x11223344)));
  EXPECT_EQ(data,
            (std::vector<byte>{0x01, 0x00, 0x00, 0x00,        // byte + pad
                                0x44, 0x33, 0x22, 0x11}));     // uint32
}
 
TEST_F(MarshalTestSuite, MarshalNestedStructAlignsToEightBytes)
{
  // Here the inner struct is a *field* of the outer struct, so its
  // start offset is no longer trivially 0 -- this is what actually
  // forces the struct type's 8-byte alignment rule to kick in.
  //
  // Outer struct: (byte, struct(uint16_t))
  //   offset 0:   the outer byte field
  //   offset 1-7: padding -- STRUCT fields must start at an offset
  //               that's a multiple of 8, regardless of the inner
  //               struct's own first field only needing 2-byte
  //               alignment
  //   offset 8-9: the inner struct's uint16_t field
  auto data = MarshalDBusType(
      std::make_tuple(static_cast<uint8_t>(0x01),
                       std::make_tuple(static_cast<uint16_t>(0x0203))));
 
  EXPECT_EQ(data,
            (std::vector<byte>{0x01,                                     // outer byte
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // pad to offset 8
                                0x03, 0x02}));                            // inner uint16
}
 
TEST_F(MarshalTestSuite, MarshalStructWithEightByteField)
{
  // UINT16 followed by UINT64 requires 6 bytes of padding before the
  // UINT64 field to reach an 8-byte boundary.
  auto data = MarshalDBusType(std::make_tuple(static_cast<uint16_t>(0x0102),
                                               static_cast<uint64_t>(1)));
  EXPECT_EQ(data,
            (std::vector<byte>{0x02, 0x01,                          // uint16
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // pad
                                0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));
}
 
// ---------------------------------------------------------------------
// DICT (std::map): ARRAY of DICT_ENTRY. A DICT_ENTRY behaves like a
// 2-field STRUCT -- it is always aligned to an 8-byte boundary, and
// its key/value fields use their own natural alignment within that.
// std::map iterates in ascending key order, so marshalling should
// follow that order regardless of insertion order.
// ---------------------------------------------------------------------
 
TEST_F(MarshalTestSuite, MarshalEmptyMap)
{
  // Even with zero entries, the D-Bus spec still requires alignment
  // padding up to the element type's boundary (8, for DICT_ENTRY)
  // after the array length field -- it's just not counted in the
  // length itself. If your implementation omits this padding for
  // empty arrays, drop the trailing 4 zero bytes here.
  EXPECT_EQ(MarshalDBusType(std::map<uint32_t, uint32_t>{}),
            (std::vector<byte>{0x00, 0x00, 0x00, 0x00,   // array length = 0
                                0x00, 0x00, 0x00, 0x00})); // pad to 8-byte boundary
}
 
TEST_F(MarshalTestSuite, MarshalMapOfUint32ToUint32)
{
  // DICT_ENTRY(uint32_t, uint32_t) is 8 bytes (4 + 4), which is
  // already a multiple of 8, so consecutive entries need no padding
  // between them here.
  EXPECT_EQ(MarshalDBusType(std::map<uint32_t, uint32_t>{{1, 10}, {2, 20}}),
            (std::vector<byte>{
                0x10, 0x00, 0x00, 0x00,  // array length = 16 (2 entries * 8 bytes)
                0x00, 0x00, 0x00, 0x00,  // pad to 8-byte boundary for first entry
                0x01, 0x00, 0x00, 0x00,  // entry 1 key   = 1
                0x0A, 0x00, 0x00, 0x00,  // entry 1 value = 10
                0x02, 0x00, 0x00, 0x00,  // entry 2 key   = 2
                0x14, 0x00, 0x00, 0x00,  // entry 2 value = 20
            }));
}
 
TEST_F(MarshalTestSuite, MarshalMapEntriesArePaddedToEightBytesBetweenElements)
{
  // DICT_ENTRY(uint32_t, uint16_t) occupies only 6 bytes (4 + 2), so
  // each entry after the first needs 2 bytes of padding to bring the
  // next entry back to an 8-byte boundary -- exactly like an array of
  // undersized structs. This padding IS counted in the array length.
  EXPECT_EQ(MarshalDBusType(std::map<uint32_t, uint16_t>{{1, 0xBEEF}, {2, 0xDEAD}}),
            (std::vector<byte>{
                0x0E, 0x00, 0x00, 0x00,  // array length = 14
                0x00, 0x00, 0x00, 0x00,  // pad to 8-byte boundary for first entry
                0x01, 0x00, 0x00, 0x00,  // entry 1 key   = 1
                0xEF, 0xBE,              // entry 1 value = 0xBEEF
                0x00, 0x00,              // pad entry 1 -> 8 bytes (offset 14 to 16)
                0x02, 0x00, 0x00, 0x00,  // entry 2 key   = 2
                0xAD, 0xDE,              // entry 2 value = 0xDEAD
            }));
}
 
TEST_F(MarshalTestSuite, MarshalMapOfStringToUint32)
{
  // DICT_ENTRY(string, uint32_t): the STRING key only needs 4-byte
  // alignment for its own length field, which is already satisfied by
  // the entry's 8-byte start; but the UINT32 value after a 1-char
  // string needs 2 bytes of padding to reach its own 4-byte boundary.
  EXPECT_EQ(MarshalDBusType(std::map<std::string, uint32_t>{{"a", 1}}),
            (std::vector<byte>{
                0x0C, 0x00, 0x00, 0x00,        // array length = 12
                0x00, 0x00, 0x00, 0x00,        // pad to 8-byte boundary for the entry
                0x01, 0x00, 0x00, 0x00, 'a', 0x00,  // key "a" (6 bytes)
                0x00, 0x00,                     // pad value to 4-byte boundary
                0x01, 0x00, 0x00, 0x00,        // value = 1
            }));
}
 
TEST_F(MarshalTestSuite, MarshalMapOrdersEntriesByKeyRegardlessOfInsertionOrder)
{
  // std::map always iterates in ascending key order internally, so
  // inserting keys out of order should not change the marshalled
  // output -- it should be identical to inserting them in order.
  std::map<uint32_t, uint32_t> out_of_order{{2, 20}, {1, 10}};
  EXPECT_EQ(MarshalDBusType(out_of_order),
            MarshalDBusType(std::map<uint32_t, uint32_t>{{1, 10}, {2, 20}}));
}
 
// ---------------------------------------------------------------------
// VARIANT: SIGNATURE + the contained value, value aligned to its own
// type's boundary. VARIANT itself carries no alignment requirement.
// ---------------------------------------------------------------------
 

TEST_F(MarshalTestSuite, MarshalVariantOfByte)
{
  // Signature "y" is 3 bytes (len=1, 'y', NUL). BYTE needs no
  // alignment, so the value follows immediately with no padding.
  EXPECT_EQ(MarshalDBusType(Variant(static_cast<uint8_t>(0x07))),
            (std::vector<byte>{0x01, 'y', 0x00,   // signature "y"
                                0x07}));           // value
}
 
TEST_F(MarshalTestSuite, MarshalVariantOfUint32)
{
  // Signature "u" is 3 bytes (len=1, 'u', NUL), landing us at offset
  // 3. UINT32 needs 4-byte alignment, so 1 padding byte is inserted
  // before the value.
  EXPECT_EQ(MarshalDBusType(Variant(static_cast<uint32_t>(42))),
            (std::vector<byte>{0x01, 'u', 0x00,         // signature "u"
                                0x00,                     // pad to 4-byte boundary
                                0x2A, 0x00, 0x00, 0x00})); // value = 42
}
 
TEST_F(MarshalTestSuite, MarshalVariantOfDouble)
{
  // Signature "d" is 3 bytes, landing at offset 3. DOUBLE needs
  // 8-byte alignment, so 5 padding bytes are inserted before the
  // value.
  EXPECT_EQ(MarshalDBusType(Variant(2.5)),
            (std::vector<byte>{
                0x01, 'd', 0x00,                          // signature "d"
                0x00, 0x00, 0x00, 0x00, 0x00,              // pad to 8-byte boundary
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x40,  // value = 2.5
            }));
}
 
TEST_F(MarshalTestSuite, MarshalVariantOfString)
{
  // Signature "s" is 3 bytes (len=1, 's', NUL), landing us at offset
  // 3. STRING's length field is a UINT32, which needs 4-byte
  // alignment -- offset 3 is NOT a multiple of 4, so 1 padding byte
  // is required before the string's own length field.
  EXPECT_EQ(MarshalDBusType(Variant(std::string("Hi!"))),
            (std::vector<byte>{
                0x01, 's', 0x00,                    // signature "s"
                0x00,                                // pad to 4-byte boundary
                0x03, 0x00, 0x00, 0x00,              // string length = 3
                'H', 'i', '!',                       // content
                0x00,                                // NUL terminator
            }));
}
 
TEST_F(MarshalTestSuite, MarshalVariantOfEmptyString)
{
  // Same "s" signature as above -- same 1-byte pad before the length
  // field, regardless of the string's own content being empty.
  EXPECT_EQ(MarshalDBusType(Variant(std::string(""))),
            (std::vector<byte>{
                0x01, 's', 0x00,          // signature "s"
                0x00,                     // pad to 4-byte boundary
                0x00, 0x00, 0x00, 0x00,   // string length = 0
                0x00,                     // NUL terminator
            }));
}
 
TEST_F(MarshalTestSuite, MarshalVariantOfArrayOfUint32)
{
  // Signature "au" is 4 bytes (len=2, 'a', 'u', NUL), landing us at
  // offset 4 -- already 4-byte aligned for the array's own length
  // field, so no extra padding is needed before it.
  EXPECT_EQ(MarshalDBusType(Variant(std::vector<uint32_t>{1, 2})),
            (std::vector<byte>{
                0x02, 'a', 'u', 0x00,               // signature "au"
                0x08, 0x00, 0x00, 0x00,              // array byte length = 8
                0x01, 0x00, 0x00, 0x00,
                0x02, 0x00, 0x00, 0x00,
            }));
}
 
TEST_F(MarshalTestSuite, MarshalVariantOfStruct)
{
  // Signature "(yu)" is 6 bytes (len=4, '(', 'y', 'u', ')', NUL),
  // landing us at offset 6. STRUCT needs 8-byte alignment, so 2
  // padding bytes are inserted before the struct's fields begin.
  auto data = MarshalDBusType(
      Variant(std::make_tuple(static_cast<uint8_t>(0x01), static_cast<uint32_t>(0x11223344))));
  EXPECT_EQ(data,
            (std::vector<byte>{
                0x04, '(', 'y', 'u', ')', 0x00,   // signature "(yu)"
                0x00, 0x00,                        // pad to 8-byte boundary
                0x01, 0x00, 0x00, 0x00,             // struct: byte + pad
                0x44, 0x33, 0x22, 0x11,             // struct: uint32
            }));
}
 
TEST_F(MarshalTestSuite, MarshalNestedVariant)
{
  // A variant holding another variant. Outer signature "v" is 3
  // bytes; VARIANT has no alignment requirement of its own, so the
  // inner variant follows immediately with no padding.
  EXPECT_EQ(MarshalDBusType(Variant(in_place, Variant(static_cast<uint8_t>(0x07)))),
            (std::vector<byte>{
                0x01, 'v', 0x00,   // outer signature "v"
                0x01, 'y', 0x00,   // inner signature "y"
                0x07,              // inner value
  }));
}
 
TEST_F(MarshalTestSuite, MarshalStructContainingVariantAlignsValueToBufferOffset)
{
  // Confirms the contained value's alignment is computed relative to
  // its actual position in the overall buffer, not relative to the
  // start of the variant itself.
  //
  // Struct: (byte, byte, variant(uint32_t))
  //   offset 0:    first byte field
  //   offset 1:    second byte field
  //   offset 2-4:  variant's signature "u" (VARIANT itself needs no
  //                alignment, so it starts immediately at offset 2)
  //   offset 5-7:  padding -- offset 5 isn't 4-byte aligned, so 3
  //                bytes are needed before the contained uint32_t
  //   offset 8-11: the uint32_t value
  auto data = MarshalDBusType(std::make_tuple(static_cast<uint8_t>(0xAA), static_cast<uint8_t>(0xBB),
                                               Variant(static_cast<uint32_t>(0x11223344))));
  EXPECT_EQ(data,
            (std::vector<byte>{
                0xAA,                    // byte 1
                0xBB,                    // byte 2
                0x01, 'u', 0x00,          // variant signature "u"
                0x00, 0x00, 0x00,         // pad to 4-byte boundary (offset 5 -> 8)
                0x44, 0x33, 0x22, 0x11,   // variant's uint32_t value
            }));
}

// ---------------------------------------------------------------------
// Deeply nested composites: maps of maps, structs of structs, and
// structs/maps alternating. Each level re-triggers the same alignment
// rules already exercised individually above (ARRAY needs 4-byte
// alignment for its length field; STRUCT/DICT_ENTRY need 8-byte
// alignment for their start), so getting these right is mostly a
// matter of carefully tracking the running offset through each layer.
// ---------------------------------------------------------------------
 
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
 
  EXPECT_EQ(MarshalDBusType(map),
            (std::vector<byte>{
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
  auto data = MarshalDBusType(std::make_tuple(
      static_cast<uint8_t>(0xAA),
      std::make_tuple(static_cast<uint8_t>(0xBB),
                       std::make_tuple(static_cast<uint8_t>(0xCC), static_cast<uint32_t>(0x11223344)))));
 
  EXPECT_EQ(data,
            (std::vector<byte>{
                0xAA,                                      // outermost byte
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // pad to offset 8
                0xBB,                                       // middle byte
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // pad to offset 16
                0xCC,                                       // innermost byte
                0x00, 0x00, 0x00,                            // pad to offset 20
                0x44, 0x33, 0x22, 0x11,                       // uint32_t value
            }));
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
 
  auto data = MarshalDBusType(std::make_tuple(
      static_cast<uint8_t>(0xAA), MapA{{1, InnerStruct{static_cast<uint8_t>(0xBB), MapB{{2, 20}}}}}));
 
  EXPECT_EQ(data,
            (std::vector<byte>{
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
