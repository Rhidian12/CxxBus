#include <gtest/gtest.h>

#include <vector>

#include "src/DBus.h"

using namespace cxxbus;

struct UnmarshalTestSuite : ::testing::Test
{
};

TEST_F(UnmarshalTestSuite, UnmarshalBoolean)
{
  EXPECT_EQ(UnmarshalDBusType<bool>({0x01, 0x00, 0x00, 0x00}, "b"), true);
  EXPECT_EQ(UnmarshalDBusType<bool>({0x00, 0x00, 0x00, 0x00}, "b"), false);
}

TEST_F(UnmarshalTestSuite, UnmarshalIntegers)
{
  EXPECT_EQ(UnmarshalDBusType<uint8_t>({0x42}, "y"), 0x42);

  EXPECT_EQ(UnmarshalDBusType<int16_t>({0xFF, 0xFF}, "n"), -1);
  EXPECT_EQ(UnmarshalDBusType<int16_t>({0x34, 0x12}, "n"), 0x1234);

  EXPECT_EQ(UnmarshalDBusType<uint16_t>({0x34, 0x12}, "q"), 0x1234);

  EXPECT_EQ(UnmarshalDBusType<int32_t>({0x00, 0x00, 0x00, 0x80}, "i"), std::numeric_limits<int32_t>::min());
  EXPECT_EQ(UnmarshalDBusType<int32_t>({0x2A, 0x00, 0x00, 0x00}, "i"), 42);

  EXPECT_EQ(UnmarshalDBusType<uint32_t>({0x2A, 0x00, 0x00, 0x00}, "u"), 42u);

  EXPECT_EQ(UnmarshalDBusType<int64_t>({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, "x"), -1);

  EXPECT_EQ(UnmarshalDBusType<uint64_t>({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, "t"),
            std::numeric_limits<uint64_t>::max());
}

TEST_F(UnmarshalTestSuite, UnmarshalDouble)
{
  EXPECT_DOUBLE_EQ(UnmarshalDBusType<double>({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F}, "d"), 1.0);
}

TEST_F(UnmarshalTestSuite, UnmarshalMultipleCompleteTypes)
{
  std::vector<byte> bytes{
      0xAA,                    // u8
      0x00, 0x00, 0x00,        // 3 pad bytes
      0x03, 0x00, 0x00, 0x00,  // string length = 3
      'H',  'i',  '!',  0x00,  // "Hi!" + null terminator
  };

  auto result = UnmarshalDBusType<MultipleCompleteTypes<uint8_t, std::string>>(bytes, "ys");
  EXPECT_EQ(std::get<0>(result.GetTypes()), 0xAA);
  EXPECT_EQ(std::get<1>(result.GetTypes()), "Hi!");

  bytes = {
      0x0A, 0x00, 0x00, 0x00,  // first int32 = 10
      0xF6, 0xFF, 0xFF, 0xFF,  // second int32 = -10
  };

  auto result2 = UnmarshalDBusType<MultipleCompleteTypes<int32_t, int32_t>>(bytes, "ii");
  EXPECT_EQ(std::get<0>(result2.GetTypes()), 10);
  EXPECT_EQ(std::get<1>(result2.GetTypes()), -10);
}

TEST_F(UnmarshalTestSuite, UnmarshalString)
{
  // u32 + null terminator
  EXPECT_EQ(UnmarshalDBusType<std::string>({0x00, 0x00, 0x00, 0x00, 0x00}, "s"), "");

  std::vector<byte> bytes{0x03, 0x00, 0x00, 0x00, 'H', 'i', '!', 0x00};
  EXPECT_EQ(UnmarshalDBusType<std::string>(bytes, "s"), "Hi!");

  // "é" (U+00E9) is 2 bytes in UTF-8 (0xC3 0xA9)
  bytes = {0x02, 0x00, 0x00, 0x00, 0xC3, 0xA9, 0x00};
  EXPECT_EQ(UnmarshalDBusType<std::string>(bytes, "s"), "\xC3\xA9");
}

TEST_F(UnmarshalTestSuite, UnmarshalObjectPath)
{
  std::string const path = "/org/example/Foo";
  std::vector<byte> bytes = {0x10, 0x00, 0x00, 0x00, '/', 'o', 'r', 'g', '/', 'e', 'x',
                             'a',  'm',  'p',  'l',  'e', '/', 'F', 'o', 'o', 0x00};

  EXPECT_EQ(UnmarshalDBusType<ObjectPath>(bytes, "o"), ObjectPath(path));
}

TEST_F(UnmarshalTestSuite, UnmarshalSignatureType)
{
  std::vector<byte> bytes{0x02, 'a', 'i', 0x00};
  EXPECT_EQ(UnmarshalDBusType<Signature>(bytes, "g"), Signature("ai"));
}

TEST_F(UnmarshalTestSuite, UnmarshalArray)
{
  EXPECT_EQ(UnmarshalDBusType<std::vector<uint32_t>>({0x00, 0x00, 0x00, 0x00}, "au"), (std::vector<uint32_t>{}));

  std::vector<byte> bytes{
      0x0C, 0x00, 0x00, 0x00,  // array byte length = 12
      0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
  };
  EXPECT_EQ(UnmarshalDBusType<std::vector<uint32_t>>(bytes, "au"), (std::vector<uint32_t>{1, 2, 3}));
  EXPECT_EQ((UnmarshalDBusType<std::array<uint32_t, 3>>(bytes, "au")), (std::array<uint32_t, 3>{1, 2, 3}));

  EXPECT_THROW((UnmarshalDBusType<std::array<uint32_t, 1>>(bytes, "au")), DBusDeserializationError);
  try
  {
    UnmarshalDBusType<std::array<uint32_t, 1>>(bytes, "au");
  }
  catch (DBusDeserializationError const& ex)
  {
    EXPECT_EQ(std::string{ex.what()},
              "Trying to deserialize array with fixed length '1' but received array has '8' bytes too many. Is your "
              "array the correct length?");
  }

  EXPECT_THROW((UnmarshalDBusType<std::array<uint32_t, 6>>(bytes, "au")), DBusDeserializationError);
  try
  {
    UnmarshalDBusType<std::array<uint32_t, 6>>(bytes, "au");
  }
  catch (DBusDeserializationError const& ex)
  {
    EXPECT_EQ(std::string{ex.what()},
              "Fully deserialized received array with '3' elements, but the provided fixed array expects '6' elements. "
              "Is your array the correct length?");
  }

  bytes = {
      0x0F, 0x00, 0x00, 0x00,                   // array length = 15
      0x01, 0x00, 0x00, 0x00, 'a', 0x00,        // "a"
      0x00, 0x00,                               // padding
      0x02, 0x00, 0x00, 0x00, 'b', 'c',  0x00,  // "bc"
  };
  EXPECT_EQ(UnmarshalDBusType<std::vector<std::string>>(bytes, "as"), (std::vector<std::string>{"a", "bc"}));
}

TEST_F(UnmarshalTestSuite, UnmarshalVeryNestedArrayStructs)
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

  // clang-format off
  std::vector<byte> bytes = {
    // First Vector
    0x2C, 0x00, 0x00, 0x00, // Length of vector (44 bytes)

    // First Struct
    0x00, 0x00, 0x00, 0x00, // Padding to 8 byte boundary (tuple)

    // Second Vector
    0x24, 0x00, 0x00, 0x00, // Length of vector (36 bytes)

   0x00, 0x00, 0x00, 0x00, // Padding to 8 byte boundary 

    // Second Struct and Third (And final) Vector
   0x1C, 0x00, 0x00, 0x00, // Length of vector (28 bytes)
    
   0x00, 0x00, 0x00, 0x00, // Padding to 8 byte boundary 

    // Finally, some actual data, our tuple of integers
   0x01, 0x00, 0x00, 0x00, // First integer (1)
   0x02, 0x00, 0x00, 0x00, // Second integer (2)
   0x03, 0x00, 0x00, 0x00, // Third integer (3) 
 
   0x00, 0x00, 0x00, 0x00, // Padding to 8 byte boundary 

    // Our 2nd tuple of integers.
   0x2A, 0x00, 0x00, 0x00, // First integer (42)
   0x2B, 0x00, 0x00, 0x00, // First integer (43)
   0x2C, 0x00, 0x00, 0x00, // First integer (44)
  };
  // clang-format on

  EXPECT_EQ(UnmarshalDBusType<std::remove_cvref_t<decltype(vec)>>(bytes, "a(a(a(iii)))"), vec);
}

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

TEST_F(UnmarshalTestSuite, UnmarshalMap)
{
  std::vector<byte> bytes{
      0x00, 0x00, 0x00, 0x00,  // array length = 0
      0x00, 0x00, 0x00, 0x00,  // pad to 8-byte boundary
  };
  EXPECT_EQ((UnmarshalDBusType<std::map<uint32_t, uint32_t>>(bytes, "a{uu}")), (std::map<uint32_t, uint32_t>{}));

  bytes = {
      0x10, 0x00, 0x00, 0x00,  // array length = 16
      0x00, 0x00, 0x00, 0x00,  // pad to 8-byte boundary
      0x01, 0x00, 0x00, 0x00,  // entry 1 key   = 1
      0x0A, 0x00, 0x00, 0x00,  // entry 1 value = 10
      0x02, 0x00, 0x00, 0x00,  // entry 2 key   = 2
      0x14, 0x00, 0x00, 0x00,  // entry 2 value = 20
  };
  EXPECT_EQ((UnmarshalDBusType<std::map<uint32_t, uint32_t>>(bytes, "a{uu}")),
            (std::map<uint32_t, uint32_t>{{1, 10}, {2, 20}}));

  bytes = {
      0x0E, 0x00, 0x00, 0x00,  // array length = 14
      0x00, 0x00, 0x00, 0x00,  // pad to 8-byte boundary
      0x01, 0x00, 0x00, 0x00,  // entry 1 key   = 1
      0xEF, 0xBE,              // entry 1 value = 0xBEEF
      0x00, 0x00,              // pad entry 1 -> 8 bytes
      0x02, 0x00, 0x00, 0x00,  // entry 2 key   = 2
      0xAD, 0xDE,              // entry 2 value = 0xDEAD
  };
  EXPECT_EQ((UnmarshalDBusType<std::map<uint32_t, uint16_t>>(bytes, "a{uq}")),
            (std::map<uint32_t, uint16_t>{{1, 0xBEEF}, {2, 0xDEAD}}));
}

TEST_F(UnmarshalTestSuite, UnmarshalVariant)
{
  std::vector<byte> bytes{
      0x01, 'u',  0x00,        // signature "u"
      0x00,                    // pad to 4-byte boundary
      0x2A, 0x00, 0x00, 0x00,  // value = 42
  };
  Variant v = UnmarshalDBusType<Variant>(bytes, "v");
  EXPECT_EQ(v.GetSignature().GetSignature(), "u");
  EXPECT_EQ(v.UnmarshalData<uint32_t>(), 42u);

  bytes = {
      0x01, 's',  0x00,        // signature "s"
      0x00,                    // pad to 4-byte boundary
      0x03, 0x00, 0x00, 0x00,  // string length = 3
      'H',  'i',  '!',  0x00,  // content + NUL
  };
  v = UnmarshalDBusType<Variant>(bytes, "v");
  EXPECT_EQ(v.GetSignature().GetSignature(), "s");
  EXPECT_EQ(v.UnmarshalData<std::string>(), "Hi!");

  // Our variant contains a u32, but we're trying to unmarshal a 'std::string'
  bytes = {0x01, 'u', 0x00, 0x00, 0x2A, 0x00, 0x00, 0x00};
  EXPECT_THROW((UnmarshalDBusType<Variant>(bytes, "v").UnmarshalData<std::string>()), std::exception);

  // Variant containing empty array
  bytes = {
      0x02, 'a',  'u',  0x00,  // Signature
      0x00, 0x00, 0x00, 0x00   // empty array
  };
  EXPECT_EQ(UnmarshalDBusType<Variant>(bytes, "v").UnmarshalData<std::vector<uint32_t>>(), std::vector<uint32_t>{});

  bytes = {
      0x05, 'a',  '{',  'u',  'u', '}', 0x00,  // Signature
      0x00,                                    // Padding to 4-byte boundary
      0x00, 0x00, 0x00, 0x00,                  // array length = 0
      0x00, 0x00, 0x00, 0x00,                  // pad to 8-byte boundary
  };

  EXPECT_EQ((UnmarshalDBusType<Variant>(bytes, "v").UnmarshalData<std::map<uint32_t, uint32_t>>()),
            (std::map<uint32_t, uint32_t>{}));

  // Variant containing array with 3 i32's
  bytes = {
      0x02, 'a',  'i',  0x00,  // Signature
      0x0C, 0x00, 0x00, 0x00,  // Array length = 12
      0x01, 0x00, 0x00, 0x00,  // i32 = 1
      0x02, 0x00, 0x00, 0x00,  // i32 = 2
      0x03, 0x00, 0x00, 0x00,  // i32 = 3
  };
  EXPECT_EQ(UnmarshalDBusType<Variant>(bytes, "v").UnmarshalData<std::vector<int32_t>>(),
            (std::vector<int32_t>{1, 2, 3}));

  // Variant containing a map with 2 kv pairs: {1, 2} and {3, 4}
  bytes = {
      0x05, 'a',  '{',  'u',  'u', '}', 0x00,  // Signature
      0x00,                                    // Padding to 4-byte boundary
      0x10, 0x00, 0x00, 0x00,                  // array length = 16
      0x00, 0x00, 0x00, 0x00,                  // pad to 8-byte boundary
      0x01, 0x00, 0x00, 0x00,                  // Key u32 #1: 1
      0x02, 0x00, 0x00, 0x00,                  // Value u32 #1: 2
      0x03, 0x00, 0x00, 0x00,                  // Key u32 #2: 3
      0x04, 0x00, 0x00, 0x00,                  // Value u32 #2: 4
  };

  EXPECT_EQ((UnmarshalDBusType<Variant>(bytes, "v").UnmarshalData<std::map<uint32_t, uint32_t>>()),
            (std::map<uint32_t, uint32_t>{{1, 2}, {3, 4}}));
}

TEST_F(UnmarshalTestSuite, UnmarshalNestedVariant)
{
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

TEST_F(UnmarshalTestSuite, UnmarshalVariantWithNestedStruct)
{
  // Make a variant of a struct containing a struct containing a variant of a string
  std::vector<byte> bytes{
      0x07, '(', 'i', '(', 'u', 'v', ')', ')', 0x00,  // Signature
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,       // Pad to 8-byte boundary
      0x2A, 0x00, 0x00, 0x00,                         // i32 = 42,
      0x00, 0x00, 0x00, 0x00,                         // Pad to 8-byte boundary
      0x18, 0x00, 0x00, 0x00,                         // u32 = 24
      // No padding required here, Signatures have an alignment of 1
      0x01, 's', 0x00,               // Signature
      0x00,                          // Pad to 4-byte boundary for String
      0x05, 0x00, 0x00, 0x00,        // u32 for string length
      'D', 'B', 'u', 's', '!', 0x00  // string
  };

  auto v = UnmarshalDBusType<Variant>(bytes, "v");
  auto data = v.UnmarshalData<std::tuple<int, std::tuple<uint32_t, Variant>>>();
  EXPECT_EQ(std::get<0>(data), 42);
  EXPECT_EQ(std::get<0>(std::get<1>(data)), 24);
  EXPECT_EQ(std::get<1>(std::get<1>(data)).UnmarshalData<std::string>(), "DBus!");
}

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

TEST_F(UnmarshalTestSuite, BufferTooShort)
{
  // "u" needs 4 bytes; only 2 are provided.
  EXPECT_THROW(UnmarshalDBusType<uint32_t>({0x01, 0x02}, "u"), DBusMalformedInputError);

  // String claims 10 bytes of size, we provide less
  std::vector<byte> bytes{0x0A, 0x00, 0x00, 0x00, 'H', 'i'};
  EXPECT_THROW(UnmarshalDBusType<std::string>(bytes, "s"), DBusMalformedInputError);
}

TEST_F(UnmarshalTestSuite, BufferTooLong)
{
  // Array claims 100 bytes of content, buffer doesn't have that much.
  std::vector<byte> bytes{0x64, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
  EXPECT_THROW((UnmarshalDBusType<std::vector<uint32_t>>(bytes, "au")), DBusMalformedInputError);

  // A u8 needs only 1 byte, leaving 1 unparsed byte, this should be an error
  EXPECT_THROW(UnmarshalDBusType<uint8_t>({0x01, 0x02}, "y"), DBusMalformedInputError);
}

TEST_F(UnmarshalTestSuite, ThrowsOnInvalidSignatureCharacter)
{
  // 'Z' is not a valid D-Bus type code.
  EXPECT_THROW(UnmarshalDBusType<uint8_t>({0x01}, "Z"), DBusInvalidSignatureError);

  std::vector<byte> bytes{0x00, 0x00, 0x00, 0x00, 0x00};
  EXPECT_THROW(UnmarshalDBusType<uint32_t>(bytes, "s"), DBusInvalidSignatureError);
}

TEST_F(UnmarshalTestSuite, ThrowsOnUnbalancedStructParens)
{
  std::vector<byte> bytes{0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00};
  EXPECT_THROW((UnmarshalDBusType<std::tuple<uint32_t, uint32_t>>(bytes, "(uu")), DBusInvalidSignatureError);
}
