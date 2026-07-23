#include <gtest/gtest.h>

#include <iostream>

#include "src/DBus.h"

struct UnmarshalTestSuite : ::testing::Test
{
};

// ---------------------------------------------------------------------
// Fixed-size basic types
// ---------------------------------------------------------------------

TEST_F(UnmarshalTestSuite, UnmarshalByte)
{
  std::cout << ConstexprTypeName<std::string>() << "\n";
  EXPECT_EQ(UnmarshalDBusType<uint8_t>({0x42}, "y"), 0x42);
}

TEST_F(UnmarshalTestSuite, UnmarshalBooleanTrue)
{
  EXPECT_EQ(UnmarshalDBusType<bool>({0x01, 0x00, 0x00, 0x00}, "b"), true);
}

TEST_F(UnmarshalTestSuite, UnmarshalBooleanFalse)
{
  EXPECT_EQ(UnmarshalDBusType<bool>({0x00, 0x00, 0x00, 0x00}, "b"), false);
}

TEST_F(UnmarshalTestSuite, UnmarshalInt16Negative)
{
  EXPECT_EQ(UnmarshalDBusType<int16_t>({0xFF, 0xFF}, "n"), -1);
}

TEST_F(UnmarshalTestSuite, UnmarshalUint16)
{
  EXPECT_EQ(UnmarshalDBusType<uint16_t>({0x34, 0x12}, "q"), 0x1234);
}

TEST_F(UnmarshalTestSuite, UnmarshalInt32MinValue)
{
  EXPECT_EQ(UnmarshalDBusType<int32_t>({0x00, 0x00, 0x00, 0x80}, "i"), std::numeric_limits<int32_t>::min());
}

TEST_F(UnmarshalTestSuite, UnmarshalUint32)
{
  EXPECT_EQ(UnmarshalDBusType<uint32_t>({0x2A, 0x00, 0x00, 0x00}, "u"), 42u);
}

TEST_F(UnmarshalTestSuite, UnmarshalInt64Negative)
{
  EXPECT_EQ(UnmarshalDBusType<int64_t>({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, "x"), -1);
}

TEST_F(UnmarshalTestSuite, UnmarshalUint64MaxValue)
{
  EXPECT_EQ(UnmarshalDBusType<uint64_t>({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, "t"), std::numeric_limits<uint64_t>::max());
}

TEST_F(UnmarshalTestSuite, UnmarshalDouble)
{
  // IEEE-754 bytes for 1.0.
  EXPECT_DOUBLE_EQ(UnmarshalDBusType<double>({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F}, "d"), 1.0);
}

// ---------------------------------------------------------------------
// Alignment: padding bytes in the body must be skipped, not consumed
// as data
// ---------------------------------------------------------------------

TEST_F(UnmarshalTestSuite, UnmarshalMultipleCompleteTypesSkipsPaddingBetweenFields)
{
  // Body signature "ys" (NOT "(ys)" -- this is a bare sequence of
  // complete types, not a STRUCT, so it unmarshals into
  // MultipleCompleteTypes<...> rather than std::tuple<...>). The
  // string's UINT32 length field needs 4-byte alignment, so 3 padding
  // bytes sit between the byte and the string -- these must be
  // skipped, not misread as part of the string's length.
  std::vector<byte> bytes{
      0xAA, 0x00, 0x00, 0x00,  // byte (0xAA) + 3 pad bytes
      0x03, 0x00, 0x00, 0x00,  // string length = 3
      'H',  'i',  '!',  0x00,  // "Hi!" + NUL
  };

  auto result = UnmarshalDBusType<MultipleCompleteTypes<uint8_t, std::string>>(bytes, "ys");
  EXPECT_EQ(std::get<0>(result.GetTypes()), 0xAA);
  EXPECT_EQ(std::get<1>(result.GetTypes()), "Hi!");
}

TEST_F(UnmarshalTestSuite, UnmarshalMultipleCompleteTypesWithNoInterFieldPadding)
{
  // Body signature "ii": two INT32 fields, already 4-byte aligned
  // back-to-back, so no padding appears anywhere.
  std::vector<byte> bytes{
      0x0A, 0x00, 0x00, 0x00,  // first int32 = 10
      0xF6, 0xFF, 0xFF, 0xFF,  // second int32 = -10
  };

  auto result = UnmarshalDBusType<MultipleCompleteTypes<int32_t, int32_t>>(bytes, "ii");
  EXPECT_EQ(std::get<0>(result.GetTypes()), 10);
  EXPECT_EQ(std::get<1>(result.GetTypes()), -10);
}

// ---------------------------------------------------------------------
// STRING / OBJECT_PATH / SIGNATURE
// ---------------------------------------------------------------------

TEST_F(UnmarshalTestSuite, UnmarshalEmptyString)
{
  EXPECT_EQ(UnmarshalDBusType<std::string>({0x00, 0x00, 0x00, 0x00, 0x00}, "s"), "");
}

TEST_F(UnmarshalTestSuite, UnmarshalString)
{
  std::vector<byte> bytes{0x03, 0x00, 0x00, 0x00, 'H', 'i', '!', 0x00};
  EXPECT_EQ(UnmarshalDBusType<std::string>(bytes, "s"), "Hi!");
}

TEST_F(UnmarshalTestSuite, UnmarshalObjectPath)
{
  std::string const path = "/org/example/Foo";
  std::vector<byte> bytes = {static_cast<byte>(path.size()), 0x00, 0x00, 0x00};
  bytes.insert(bytes.end(), path.begin(), path.end());
  bytes.push_back(0x00);

  EXPECT_EQ(UnmarshalDBusType<ObjectPath>(bytes, "o"), ObjectPath(path));
}

TEST_F(UnmarshalTestSuite, UnmarshalSignatureType)
{
  std::vector<byte> bytes{0x02, 'a', 'i', 0x00};
  EXPECT_EQ(UnmarshalDBusType<Signature>(bytes, "g"), Signature("ai"));
}

TEST_F(UnmarshalTestSuite, UnmarshalStringWithMultiByteUtf8Character)
{
  // "é" (U+00E9) is 2 bytes in UTF-8 (0xC3 0xA9) but the string
  // length field measures bytes, not code points.
  std::vector<byte> bytes{0x02, 0x00, 0x00, 0x00, 0xC3, 0xA9, 0x00};
  EXPECT_EQ(UnmarshalDBusType<std::string>(bytes, "s"), "\xC3\xA9");
}

// ---------------------------------------------------------------------
// ARRAY
// ---------------------------------------------------------------------

TEST_F(UnmarshalTestSuite, UnmarshalEmptyArrayOfUint32)
{
  EXPECT_EQ(UnmarshalDBusType<std::vector<uint32_t>>({0x00, 0x00, 0x00, 0x00}, "au"), (std::vector<uint32_t>{}));
}

TEST_F(UnmarshalTestSuite, UnmarshalArrayOfUint32)
{
  std::vector<byte> bytes{
      0x0C, 0x00, 0x00, 0x00,  // array byte length = 12
      0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
  };
  EXPECT_EQ(UnmarshalDBusType<std::vector<uint32_t>>(bytes, "au"), (std::vector<uint32_t>{1, 2, 3}));
}

TEST_F(UnmarshalTestSuite, UnmarshalArrayOfStringsWithInterElementPadding)
{
  // "a" occupies 6 bytes (4 len + 1 char + 1 NUL); "bc" needs 2 pad
  // bytes before it to bring its length field back to a 4-byte
  // boundary. These padding bytes are counted in the array length but
  // must NOT show up in the decoded elements.
  std::vector<byte> bytes{
      0x0F, 0x00, 0x00, 0x00,                   // array length = 15
      0x01, 0x00, 0x00, 0x00, 'a', 0x00,        // "a"
      0x00, 0x00,                               // padding
      0x02, 0x00, 0x00, 0x00, 'b', 'c',  0x00,  // "bc"
  };
  EXPECT_EQ(UnmarshalDBusType<std::vector<std::string>>(bytes, "as"), (std::vector<std::string>{"a", "bc"}));
}

// ---------------------------------------------------------------------
// STRUCT
// ---------------------------------------------------------------------

TEST_F(UnmarshalTestSuite, UnmarshalStruct)
{
  std::vector<byte> bytes{
      0x01, 0x00, 0x00, 0x00,  // byte field + 3 pad bytes
      0x44, 0x33, 0x22, 0x11,  // uint32 field
  };
  auto result = UnmarshalDBusType<std::tuple<uint8_t, uint32_t>>(bytes, "(yu)");
  EXPECT_EQ(std::get<0>(result), 0x01);
  EXPECT_EQ(std::get<1>(result), 0x11223344u);
}

TEST_F(UnmarshalTestSuite, UnmarshalNestedStructAlignsToEightBytes)
{
  // Outer struct: (byte, struct(uint16_t)). The inner struct field
  // must start at an 8-byte-aligned offset, so 7 padding bytes
  // separate the outer byte from the inner struct's own field.
  std::vector<byte> bytes{
      0x01,                                      // outer byte
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // pad to offset 8
      0x03, 0x02,                                // inner uint16_t = 0x0203
  };
  auto result = UnmarshalDBusType<std::tuple<uint8_t, std::tuple<uint16_t>>>(bytes, "(y(q))");
  EXPECT_EQ(std::get<0>(result), 0x01);
  EXPECT_EQ(std::get<0>(std::get<1>(result)), 0x0203);
}

// ---------------------------------------------------------------------
// DICT (std::map)
// ---------------------------------------------------------------------

TEST_F(UnmarshalTestSuite, UnmarshalEmptyMap)
{
  std::vector<byte> bytes{
      0x00, 0x00, 0x00, 0x00,  // array length = 0
      0x00, 0x00, 0x00, 0x00,  // pad to 8-byte boundary
  };
  EXPECT_EQ((UnmarshalDBusType<std::map<uint32_t, uint32_t>>(bytes, "a{uu}")), (std::map<uint32_t, uint32_t>{}));
}

TEST_F(UnmarshalTestSuite, UnmarshalMapOfUint32ToUint32)
{
  std::vector<byte> bytes{
      0x10, 0x00, 0x00, 0x00,  // array length = 16
      0x00, 0x00, 0x00, 0x00,  // pad to 8-byte boundary
      0x01, 0x00, 0x00, 0x00,  // entry 1 key   = 1
      0x0A, 0x00, 0x00, 0x00,  // entry 1 value = 10
      0x02, 0x00, 0x00, 0x00,  // entry 2 key   = 2
      0x14, 0x00, 0x00, 0x00,  // entry 2 value = 20
  };
  EXPECT_EQ((UnmarshalDBusType<std::map<uint32_t, uint32_t>>(bytes, "a{uu}")), (std::map<uint32_t, uint32_t>{{1, 10}, {2, 20}}));
}

TEST_F(UnmarshalTestSuite, UnmarshalMapEntriesSkipsInterEntryPadding)
{
  // DICT_ENTRY(uint32_t, uint16_t) is only 6 bytes, so 2 padding
  // bytes separate the entries. These must be skipped, not
  // misinterpreted as the start of the next entry's key.
  std::vector<byte> bytes{
      0x0E, 0x00, 0x00, 0x00,  // array length = 14
      0x00, 0x00, 0x00, 0x00,  // pad to 8-byte boundary
      0x01, 0x00, 0x00, 0x00,  // entry 1 key   = 1
      0xEF, 0xBE,              // entry 1 value = 0xBEEF
      0x00, 0x00,              // pad entry 1 -> 8 bytes
      0x02, 0x00, 0x00, 0x00,  // entry 2 key   = 2
      0xAD, 0xDE,              // entry 2 value = 0xDEAD
  };
  EXPECT_EQ((UnmarshalDBusType<std::map<uint32_t, uint16_t>>(bytes, "a{uq}")), (std::map<uint32_t, uint16_t>{{1, 0xBEEF}, {2, 0xDEAD}}));
}

// ---------------------------------------------------------------------
// VARIANT
// ---------------------------------------------------------------------

TEST_F(UnmarshalTestSuite, UnmarshalVariantOfUint32)
{
  std::vector<byte> bytes{
      0x01, 'u',  0x00,        // signature "u"
      0x00,                    // pad to 4-byte boundary
      0x2A, 0x00, 0x00, 0x00,  // value = 42
  };
  auto v = UnmarshalDBusType<Variant>(bytes, "v");
  EXPECT_EQ(v.GetSignature().GetSignature(), "u");
  EXPECT_EQ(v.UnmarshalData<uint32_t>(), 42u);
}

TEST_F(UnmarshalTestSuite, UnmarshalVariantOfString)
{
  std::vector<byte> bytes{
      0x01, 's',  0x00,        // signature "s"
      0x00,                    // pad to 4-byte boundary
      0x03, 0x00, 0x00, 0x00,  // string length = 3
      'H',  'i',  '!',  0x00,  // content + NUL
  };
  auto v = UnmarshalDBusType<Variant>(bytes, "v");
  EXPECT_EQ(v.GetSignature().GetSignature(), "s");
  EXPECT_EQ(v.UnmarshalData<std::string>(), "Hi!");
}

TEST_F(UnmarshalTestSuite, UnmarshalNestedVariant)
{
  // A variant holding another variant: the outer T is itself a
  // DeserializedVariant<uint8_t>, since that's what the inner layer
  // is expected to contain.
  std::vector<byte> bytes{
      0x01, 'v', 0x00,  // outer signature "v"
      0x01, 'y', 0x00,  // inner signature "y"
      0x07,             // inner value
  };
  auto outer = UnmarshalDBusType<Variant>(bytes, "v");
  EXPECT_EQ(outer.GetSignature().GetSignature(), "v");

  auto inner = outer.UnmarshalData<Variant>();
  EXPECT_EQ(inner.GetSignature().GetSignature(), "y");
  EXPECT_EQ(inner.UnmarshalData<uint8_t>(), 0x07);
}

TEST_F(UnmarshalTestSuite, UnmarshalVariantWrongTypeThrows)
{
  // The wire contains a variant of "u" (uint32_t), but we ask
  // UnmarshalDBusType to deserialize it as a
  // DeserializedVariant<std::string>. Since T is fixed at compile
  // time here, this mismatch must be caught during unmarshalling
  // itself, not at a later .Get() call.
  std::vector<byte> bytes{0x01, 'u', 0x00, 0x00, 0x2A, 0x00, 0x00, 0x00};
  EXPECT_THROW((UnmarshalDBusType<Variant>(bytes, "v").UnmarshalData<std::string>()), std::exception);
}

// ---------------------------------------------------------------------
// Deeply nested composites -- round-trips of the exact byte layouts
// verified in the marshalling tests
// ---------------------------------------------------------------------

TEST_F(UnmarshalTestSuite, UnmarshalVeryNestedMaps)
{
  using InnerMap = std::map<uint32_t, uint32_t>;
  using MiddleMap = std::map<uint32_t, InnerMap>;
  using OuterMap = std::map<uint32_t, MiddleMap>;

  std::vector<byte> bytes{
      0x18, 0x00, 0x00, 0x00,  // outer array length = 24
      0x00, 0x00, 0x00, 0x00,  // pad to 8 for outer entry
      0x01, 0x00, 0x00, 0x00,  // outer key = 1
      0x10, 0x00, 0x00, 0x00,  // middle array length = 16
      0x02, 0x00, 0x00, 0x00,  // middle key = 2
      0x08, 0x00, 0x00, 0x00,  // inner array length = 8
      0x03, 0x00, 0x00, 0x00,  // inner key = 3
      0x1E, 0x00, 0x00, 0x00,  // inner value = 30
  };

  OuterMap expected{{1, MiddleMap{{2, InnerMap{{3, 30}}}}}};
  EXPECT_EQ(UnmarshalDBusType<OuterMap>(bytes, "a{ua{ua{uu}}}"), expected);
}

TEST_F(UnmarshalTestSuite, UnmarshalVeryNestedStructs)
{
  std::vector<byte> bytes{
      0xAA,                                      // outermost byte
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // pad to offset 8
      0xBB,                                      // middle byte
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // pad to offset 16
      0xCC,                                      // innermost byte
      0x00, 0x00, 0x00,                          // pad to offset 20
      0x44, 0x33, 0x22, 0x11,                    // uint32_t value
  };

  using Innermost = std::tuple<uint8_t, uint32_t>;
  using Middle = std::tuple<uint8_t, Innermost>;
  using Outer = std::tuple<uint8_t, Middle>;

  auto result = UnmarshalDBusType<Outer>(bytes, "(y(y(yu)))");
  EXPECT_EQ(std::get<0>(result), 0xAA);
  EXPECT_EQ(std::get<0>(std::get<1>(result)), 0xBB);
  EXPECT_EQ(std::get<0>(std::get<1>(std::get<1>(result))), 0xCC);
  EXPECT_EQ(std::get<1>(std::get<1>(std::get<1>(result))), 0x11223344u);
}

TEST_F(UnmarshalTestSuite, UnmarshalStructContainingMapContainingStructContainingMap)
{
  using MapB = std::map<uint32_t, uint32_t>;
  using InnerStruct = std::tuple<uint8_t, MapB>;
  using MapA = std::map<uint32_t, InnerStruct>;
  using Outer = std::tuple<uint8_t, MapA>;

  std::vector<byte> bytes{
      0xAA, 0x00, 0x00, 0x00,  // outer byte + pad to 4
      0x18, 0x00, 0x00, 0x00,  // MapA array length = 24
      0x01, 0x00, 0x00, 0x00,  // MapA entry key = 1
      0x00, 0x00, 0x00, 0x00,  // pad to 16 for InnerStruct
      0xBB, 0x00, 0x00, 0x00,  // InnerStruct byte + pad to 4
      0x08, 0x00, 0x00, 0x00,  // MapB array length = 8
      0x02, 0x00, 0x00, 0x00,  // MapB entry key = 2
      0x14, 0x00, 0x00, 0x00,  // MapB entry value = 20
  };

  Outer expected{0xAA, MapA{{1, InnerStruct{0xBB, MapB{{2, 20}}}}}};
  EXPECT_EQ(UnmarshalDBusType<Outer>(bytes, "(ya{u(ya{uu})})"), expected);
}

// ---------------------------------------------------------------------
// Round-trip sanity: Marshal then Unmarshal should be the identity
// ---------------------------------------------------------------------

TEST_F(UnmarshalTestSuite, RoundTripUint32)
{
  uint32_t original = 123456789;
  auto bytes = MarshalDBusType(original);
  EXPECT_EQ(UnmarshalDBusType<uint32_t>(bytes, "u"), original);
}

TEST_F(UnmarshalTestSuite, RoundTripStringWithArray)
{
  std::vector<std::string> original{"foo", "bar", "bazzz"};
  auto bytes = MarshalDBusType(original);
  EXPECT_EQ(UnmarshalDBusType<std::vector<std::string>>(bytes, "as"), original);
}

TEST_F(UnmarshalTestSuite, RoundTripMap)
{
  std::map<uint32_t, std::string> original{{1, "one"}, {2, "two"}, {3, "three"}};
  auto bytes = MarshalDBusType(original);
  EXPECT_EQ((UnmarshalDBusType<std::map<uint32_t, std::string>>(bytes, "a{us}")), original);
}

// ---------------------------------------------------------------------
// Error handling: malformed / inconsistent input
// ---------------------------------------------------------------------

TEST_F(UnmarshalTestSuite, ThrowsOnBufferTooShortForType)
{
  // "u" needs 4 bytes; only 2 are provided.
  EXPECT_THROW(UnmarshalDBusType<uint32_t>({0x01, 0x02}, "u"), DBusMalformedInputError);
}

TEST_F(UnmarshalTestSuite, ThrowsOnTruncatedString)
{
  // Length field claims 10 bytes of content, but the buffer only has
  // 2 content bytes and no NUL terminator.
  std::vector<byte> bytes{0x0A, 0x00, 0x00, 0x00, 'H', 'i'};
  EXPECT_THROW(UnmarshalDBusType<std::string>(bytes, "s"), DBusMalformedInputError);
}

TEST_F(UnmarshalTestSuite, ThrowsOnArrayLengthExceedingBuffer)
{
  // Array claims 100 bytes of content, buffer doesn't have that much.
  std::vector<byte> bytes{0x64, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
  EXPECT_THROW((UnmarshalDBusType<std::vector<uint32_t>>(bytes, "au")), DBusMalformedInputError);
}

TEST_F(UnmarshalTestSuite, ThrowsOnUnconsumedTrailingBytes)
{
  // A single byte's worth of signature "y" but the buffer has 2
  // bytes -- the extra trailing byte is not accounted for by the
  // signature and should be treated as malformed input rather than
  // silently ignored.
  EXPECT_THROW(UnmarshalDBusType<uint8_t>({0x01, 0x02}, "y"), DBusMalformedInputError);
}

TEST_F(UnmarshalTestSuite, ThrowsOnInvalidSignatureCharacter)
{
  // 'Z' is not a valid D-Bus type code.
  EXPECT_THROW(UnmarshalDBusType<uint8_t>({0x01}, "Z"), DBusInvalidSignatureError);
}

TEST_F(UnmarshalTestSuite, ThrowsOnUnbalancedStructParens)
{
  std::vector<byte> bytes{0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00};
  EXPECT_THROW((UnmarshalDBusType<std::tuple<uint32_t, uint32_t>>(bytes, "(uu")), DBusInvalidSignatureError);
}

TEST_F(UnmarshalTestSuite, ThrowsWhenSignatureDoesNotMatchRequestedType)
{
  // Requesting a uint32_t but supplying a signature that describes a
  // string -- the signature is the authority on wire shape, so this
  // mismatch should be rejected rather than reinterpreting the bytes.
  std::vector<byte> bytes{0x00, 0x00, 0x00, 0x00, 0x00};
  EXPECT_THROW(UnmarshalDBusType<uint32_t>(bytes, "s"), DBusInvalidSignatureError);
}
