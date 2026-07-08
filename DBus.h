#pragma once

#include <sys/types.h>

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using byte = uint8_t;

enum class DBusMessageType : uint8_t
{
  INVALID = 0,
  METHOD_CALL = 1,    // May prompt a reply
  METHOD_RETURN = 2,  // Method reply with returned data
  ERROR = 3,          // Error reply. If first argument exists and is a string, it is an error message
  SIGNAL = 4,         // Signal emission

  OPTIONAL = 254,  // Internal Usage. Not part of DBus Spec.
  NONE = 255,      // Internal Usage. Not part of DBus Spec.
};

enum class Endianness : uint8_t
{
  LITTLE_ENDIAN_TYPE = 'l',
  BIG_ENDIAN_TYPE = 'B'
};

enum class DBusMessageFlags : uint8_t
{
  NONE = 0,
  NO_REPLY_EXPECTED = 0x1,               // We don't expect replies, even if it can have replies
  NO_AUTO_START = 0x2,                   // The bus must not launch an owner for the destination name in response to this message
  ALLOW_INTERACTIVE_AUTHORIZATION = 0x4  // We are informing the receiver of our message that we are prepared to wait for interactive authorization
};

enum class HeaderFieldCode : uint8_t
{
  INVALID = 0,
  PATH = 1,          // The object to send a call to, or the object a signal is emitted from. Controlled by message sender
  INTERFACE = 2,     // The interface to invoke a method call on, or the interface a signal is emitted from. Optional for method calls, required for signals.
                     // Controlled by message sender.
  MEMBER = 3,        // The member, either method name or signal name. Controlled by message sender
  ERROR_NAME = 4,    // The name of the error that occurred.
  REPLY_SERIAL = 5,  // The serial number of the message this message is a reply to. Controlled by the message sender.
  DESTINATION = 6,   // Name of the connection message is intended for. Controlled by the message sender.
  SENDER = 7,        // Unique name of the sending connection. On a message bus, controlled by the message bus, otherwise controlled by message sender.
  SIGNATURE = 8,     // Signature of the message body. If not present, assume signature is "" (meaning body must be 0-length). Controlled by message sender
  UNIX_FDS =
      9,  // Number of Unix file descriptors that accompany the message. If not present, assume no FDs accompany the message. Controlled by message sender.

  NONE = 255  // Internal Usage. Not part of DBus Spec.
};

enum class DBusTypeCodes : uint8_t
{
  BYTE = 'y',     // uint8_t
  BOOLEAN = 'b',  // boolean
  INT16 = 'n',
  UINT16 = 'q',
  INT32 = 'i',
  UINT32 = 'u',
  INT64 = 'x',
  UINT64 = 't',
  DOUBLE = 'd',
  UNIX_FD = 'h',  // uint32_t representing Unix FD

  STRING = 's',
  OBJECT_PATH = 'o',  // DBus Object Path
  SIGNATURE = 'g',    // Zero or more Single Complete Types

  ARRAY = 'a',         // Start of Array
  STRUCT = 'r',        // Not actually used in the DBus protocol. Used to represent the general concept of a struct
  STRUCT_BEGIN = '(',  // Beginning of a Struct definition
  STRUCT_END = ')',    // End of a struct definition
  VARIANT = 'v',       // Variant. Type of the value is part of the value itself
  DICT = 'e',          // Not actually used in the DBus protocol. Used to represent the general concept of a dict.
  DICT_BEGIN = '{',    // Beginning of a dict definition
  DICT_END = '}',      // End of a dict definition

  INVALID = 255  // Internal Usage. Not part of DBus Spec.
};

inline bool IsDBusBasicFixedTypeCode(unsigned char c)
{
  switch (static_cast<DBusTypeCodes>(c))
  {
    case DBusTypeCodes::BYTE:
    case DBusTypeCodes::BOOLEAN:
    case DBusTypeCodes::INT16:
    case DBusTypeCodes::UINT16:
    case DBusTypeCodes::INT32:
    case DBusTypeCodes::UINT32:
    case DBusTypeCodes::INT64:
    case DBusTypeCodes::UINT64:
    case DBusTypeCodes::DOUBLE:
    case DBusTypeCodes::UNIX_FD:
      return true;
    default:
      return false;
  }
}

inline bool IsDBusBasicStringlikeTypeCode(unsigned char c)
{
  switch (static_cast<DBusTypeCodes>(c))
  {
    case DBusTypeCodes::STRING:
    case DBusTypeCodes::OBJECT_PATH:
    case DBusTypeCodes::SIGNATURE:
      return true;
    default:
      return false;
  }
}

inline bool IsDBusBasicTypeCode(unsigned char c)
{
  return IsDBusBasicFixedTypeCode(c) || IsDBusBasicStringlikeTypeCode(c);
}

struct HeaderField
{
  HeaderFieldCode decimalCode;
  DBusTypeCodes type;  // dbus type
  std::array<DBusMessageType, 2> requiredMessageType;
};

class Signature;
class ObjectPath;
class Variant;

template <typename T>
concept IsDBusBasicFixedType =
    std::same_as<T, uint8_t> || std::same_as<T, bool> || std::same_as<T, int16_t> || std::same_as<T, uint16_t> || std::same_as<T, int32_t> ||
    std::same_as<T, uint32_t> || std::same_as<T, int64_t> || std::same_as<T, uint64_t> || std::same_as<T, float> || std::same_as<T, double>;

template <typename T>
concept IsString = std::same_as<T, std::string> || std::same_as<T, std::string_view>;

template <typename T>
concept IsDBusBasicStringlikeType = IsString<T> || std::same_as<T, ObjectPath> || std::same_as<T, Signature>;

template <typename T>
concept IsDBusBasicType = IsDBusBasicFixedType<T> || IsDBusBasicStringlikeType<T>;

namespace detail
{
  template <typename T, template <typename...> typename Template>
  constexpr bool IS_SPECIALISATION = false;

  template <template <typename...> typename Template, typename... Args>
  constexpr bool IS_SPECIALISATION<Template<Args...>, Template> = true;
}  // namespace detail

template <typename T, template <typename...> typename Template>
concept IsSpecialisation = detail::IS_SPECIALISATION<std::remove_cvref_t<T>, Template>;

template <typename T>
concept IsDBusArray = IsSpecialisation<T, std::vector>;

template <typename T>
concept IsDBusStruct = IsSpecialisation<T, std::tuple>;

template <typename T>
concept IsDBusVariant = std::is_same_v<T, Variant>;

template <typename T>
concept IsDBusMap = IsSpecialisation<T, std::map>;

template <typename T>
concept IsDBusContainer = IsDBusArray<T> || IsDBusMap<T> || IsDBusStruct<T> || IsDBusVariant<T>;

template <typename T>
concept IsDBusType = IsDBusBasicType<std::remove_cvref_t<T>> || IsDBusContainer<std::remove_cvref_t<T>>;

template <IsDBusType T>
std::string GetTypeSignature()
{
  if constexpr (std::is_same_v<T, uint8_t>)
  {
    return "y";
  }
  else if constexpr (std::is_same_v<T, bool>)
  {
    return "b";
  }
  else if constexpr (std::is_same_v<T, int16_t>)
  {
    return "n";
  }
  else if constexpr (std::is_same_v<T, uint16_t>)
  {
    return "q";
  }
  else if constexpr (std::is_same_v<T, int32_t>)
  {
    return "i";
  }
  else if constexpr (std::is_same_v<T, uint32_t>)
  {
    return "u";
  }
  else if constexpr (std::is_same_v<T, int64_t>)
  {
    return "x";
  }
  else if constexpr (std::is_same_v<T, uint64_t>)
  {
    return "t";
  }
  else if constexpr (std::is_same_v<T, double>)
  {
    return "d";
  }
  else if constexpr (IsString<T>)
  {
    return "s";
  }
  else if constexpr (std::is_same_v<T, ObjectPath>)
  {
    return "o";
  }
  else if constexpr (std::is_same_v<T, Signature>)
  {
    return "g";
  }
  else if constexpr (IsDBusArray<T>)
  {
    return std::string{"a"} + GetTypeSignature<typename T::value_type>();
  }
  else if constexpr (IsDBusStruct<T>)
  {
    std::string type{"("};
    [&type]<size_t... Is>(std::index_sequence<Is...>)
    { type += (GetTypeSignature<std::tuple_element_t<Is, T>>() + ...); }(std::make_index_sequence<std::tuple_size_v<T>>{});
    type += ")";
    return type;
  }
  else if constexpr (std::is_same_v<T, Variant>)
  {
    return "v";
  }
  else if constexpr (IsDBusMap<T>)
  {
    return std::string{"a{"} + GetTypeSignature<typename T::key_type>() + GetTypeSignature<typename T::mapped_type>() + "}";
  }
}

inline void ApplyPadding(std::vector<byte>& bytes, uint8_t alignment)
{
  if (bytes.size() % alignment == 0) return;

  bytes.append_range(std::vector<byte>(static_cast<uint8_t>(alignment - (bytes.size() % alignment)), '\0'));
}

inline void AddPaddingToSize(uint32_t& size, uint8_t alignment)
{
  uint32_t const result{size % alignment};
  if (result == 0) return;

  size += alignment - result;
}

class ObjectPath
{
 private:
  std::string m_path;

 public:
  ObjectPath(std::string path)
    : m_path(std::move(path))
  {
  }

  uint32_t size() const
  {
    return m_path.size();
  }

  explicit operator std::string() const
  {
    return m_path;
  }
};

class Signature
{
 private:
  std::string m_signature;

 public:
  Signature(std::string signature)
    : m_signature(std::move(signature))
  {
  }

  uint32_t size() const
  {
    return m_signature.size();
  }

  explicit operator std::string() const
  {
    return m_signature;
  }
};

template <IsDBusType T>
uint8_t GetAlignmentOfDBusType()
{
  if constexpr (std::is_same_v<T, uint8_t>)
  {
    return 1;
  }
  else if constexpr (std::is_same_v<T, bool>)
  {
    return 4;
  }
  else if constexpr (std::is_same_v<T, int16_t> || std::is_same_v<T, uint16_t>)
  {
    return 2;
  }
  else if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, int32_t>)
  {
    return 4;
  }
  else if constexpr (std::is_same_v<T, uint64_t> || std::is_same_v<T, int64_t> || std::is_same_v<T, double>)
  {
    return 8;
  }
  else if constexpr (IsDBusBasicStringlikeType<T>)
  {
    if constexpr (std::is_same_v<T, Signature>)
    {
      return 1;
    }
    else
    {
      return 4;  // For the length of the leading uint32_t
    }
  }
  else if constexpr (IsDBusContainer<T>)
  {
    if constexpr (IsDBusArray<T>)
    {
      return GetAlignmentOfDBusType<typename T::value_type>();
    }
    else if (IsDBusStruct<T> || IsDBusMap<T>)
    {
      return 8;
    }
    else if (std::is_same_v<T, Variant>)
    {
      return 1;  // Alignment of Signature
    }
  }
  // [TODO]: Unix FD
}

template <IsDBusType T>
void MarshalDBusTypeImpl(T const& value, std::vector<byte>& dbusType);

template <IsDBusType T>
void GetSizeOfDBusType(T const& value, uint32_t& size);

struct InPlaceT
{
};
constexpr InPlaceT in_place{};

class Variant
{
 private:
  struct CustomDeleter
  {
    std::function<void(void*)> deleter;
    void operator()(void* data)
    {
      deleter(data);
    }
  };

  struct VariantData
  {
    Signature signature;
    uint32_t dataSize;
    uint8_t dataAlignment;
    std::unique_ptr<void, CustomDeleter> data;
    std::function<void(void*, std::vector<byte>&)> marshalDataFunc;
    std::function<std::unique_ptr<void, CustomDeleter>(void*)> copyFunc;
  };

  VariantData m_variantData;

 public:
  template <IsDBusType T>
    requires(!std::is_same_v<std::remove_cvref_t<T>, Variant>)
  explicit Variant(T&& value)
    : m_variantData{
          .signature = GetTypeSignature<std::remove_cvref_t<T>>(),
          .dataAlignment = GetAlignmentOfDBusType<std::remove_cvref_t<T>>(),
          .data = std::unique_ptr<void, CustomDeleter>(new std::remove_cvref_t<T>{std::forward<T>(value)},
                                                       CustomDeleter{.deleter = [](void* data) { delete static_cast<std::remove_cvref_t<T>*>(data); }}),
          .marshalDataFunc = [](void* data, std::vector<byte>& dbusType) { MarshalDBusTypeImpl(*static_cast<std::remove_cvref_t<T>*>(data), dbusType); },
          .copyFunc =
              [](void* otherData)
          {
            return std::unique_ptr<void, CustomDeleter>(new std::remove_cvref_t<T>{*static_cast<std::remove_cvref_t<T>*>(otherData)},
                                                        CustomDeleter{.deleter = [](void* data) { delete static_cast<std::remove_cvref_t<T>*>(data); }});
          }}
  {
    GetSizeOfDBusType(value, m_variantData.dataSize);
  }

  // Wraps a Variant inside a Variant (nested/boxed variant)
  Variant(InPlaceT, Variant variant)
    : m_variantData{.signature = GetTypeSignature<Variant>(),
                    .dataAlignment = GetAlignmentOfDBusType<Variant>(),
                    .data = std::unique_ptr<void, CustomDeleter>(new Variant(std::move(variant)),
                                                                 CustomDeleter{.deleter = [](void* data) { delete static_cast<Variant*>(data); }}),
                    .marshalDataFunc = [](void* data, std::vector<byte>& dbusType) { static_cast<Variant*>(data)->MarshalData(dbusType); },
                    .copyFunc =
                        [](void* otherData)
                    {
                      return std::unique_ptr<void, CustomDeleter>(new Variant(*static_cast<Variant*>(otherData)),
                                                                  CustomDeleter{.deleter = [](void* data) { delete static_cast<Variant*>(data); }});
                    }}
  {
    GetSizeOfDBusType<Signature>(variant.GetSignature(), m_variantData.dataSize);
    m_variantData.dataSize += variant.GetDataSize();
  }

  Variant(Variant const& other)
    : m_variantData{.signature = other.m_variantData.signature,
                    .dataSize = other.m_variantData.dataSize,
                    .dataAlignment = other.m_variantData.dataAlignment,
                    .data = other.m_variantData.copyFunc(other.m_variantData.data.get()),
                    .marshalDataFunc = other.m_variantData.marshalDataFunc,
                    .copyFunc = other.m_variantData.copyFunc}
  {
  }

  Variant(Variant&&) noexcept = default;
  Variant& operator=(Variant&& other) noexcept = default;

  Variant& operator=(Variant const& other)
  {
    if (this != &other)
    {
      Variant tmp(other);
      *this = std::move(tmp);
    }
    return *this;
  }

  Signature GetSignature() const
  {
    return m_variantData.signature;
  }
  uint32_t GetDataSize() const
  {
    return m_variantData.dataSize;
  }
  uint8_t GetDataAlignment() const
  {
    return m_variantData.dataAlignment;
  }

  void MarshalData(std::vector<byte>& dbusType) const
  {
    // We marshal a variant by marshalling its signature followed by the data (with padding of course)
    // Add signature + padding to data type
    MarshalDBusTypeImpl(m_variantData.signature, dbusType);
    ApplyPadding(dbusType, m_variantData.dataAlignment);

    m_variantData.marshalDataFunc(m_variantData.data.get(), dbusType);
  }
};

template <IsDBusType T, size_t I, size_t MaxI>
void GetStructMemberSize(T const& value, uint32_t& size)
{
  GetSizeOfDBusType(std::get<I>(value), size);
  if constexpr (I < MaxI - 1)
  {
    uint8_t const alignment{GetAlignmentOfDBusType<std::remove_cvref_t<decltype(std::get<I>(value))>>()};
    AddPaddingToSize(size, alignment);
  }
}

template <IsDBusStruct T, size_t... Is>
void GetStructSize(T const& value, uint32_t& size, std::index_sequence<Is...>)
{
  return (GetStructMemberSize<T, Is, std::tuple_size_v<T>>(value, size), ...);
}

template <IsDBusMap T>
void GetMapSize(T const& value, uint32_t& size)
{
  GetSizeOfDBusType(uint32_t{}, size);
  AddPaddingToSize(size, GetAlignmentOfDBusType<T>());

  for (auto it{value.cbegin()}; it != value.cend(); ++it)
  {
    GetSizeOfDBusType(it->first, size);

    if constexpr (IsDBusMap<typename T::mapped_type>)
    {
      // uint32_t because a map is an array
      AddPaddingToSize(size, GetAlignmentOfDBusType<uint32_t>());
    }
    else
    {
      AddPaddingToSize(size, GetAlignmentOfDBusType<typename T::mapped_type>());
    }

    GetSizeOfDBusType(it->second, size);

    if (std::distance(it, value.cend()) > 1)
    {
      uint8_t const alignment{GetAlignmentOfDBusType<T>()};
      AddPaddingToSize(size, alignment);
    }
  }
}

template <IsDBusType T>
void GetSizeOfDBusType(T const& value, uint32_t& size)
{
  if constexpr (IsDBusBasicFixedType<T>)
  {
    // For the fixed types alignment and size is the same
    size += GetAlignmentOfDBusType<T>();
  }
  else if constexpr (IsDBusBasicStringlikeType<T>)
  {
    if constexpr (std::is_same_v<T, Signature>)
    {
      // Length of string as u8 + actual length of string + '\0'
      size += sizeof(uint8_t) + value.size() + 1;
    }
    else
    {
      // Length of string as u32 + actual length of string + '\0'
      size += sizeof(uint32_t) + value.size() + 1;
    }
  }
  else if constexpr (IsDBusContainer<T>)
  {
    if constexpr (IsDBusArray<T>)
    {
      if (value.empty())
      {
        size += 0;
      }

      // length of array data in bytes
      uint8_t const alignment{GetAlignmentOfDBusType<typename T::value_type>()};
      for (auto const& [index, elem] : std::views::enumerate(value))
      {
        GetSizeOfDBusType<typename T::value_type>(elem, size);

        if (index < value.size() - 1)
        {
          AddPaddingToSize(size, alignment);
        }
      }
    }
    else if constexpr (IsDBusStruct<T>)
    {
      GetStructSize(value, size, std::make_index_sequence<std::tuple_size_v<T>>{});
    }
    else if constexpr (IsDBusMap<T>)
    {
      GetMapSize(value, size);
    }
    else if constexpr (IsDBusVariant<T>)
    {
      // Variant = Signature + Padding + Size of data
      GetSizeOfDBusType<Signature>(Signature{""}, size);
      AddPaddingToSize(size, value.GetDataAlignment());
      size += value.GetDataSize();
    }
    else
    {
      static_assert(false, "Unknown dbus container type");
    }
  }
}

constexpr HeaderField HEADER_FIELDS[] = {
    HeaderField{.decimalCode = HeaderFieldCode::INVALID, .type = DBusTypeCodes::INVALID, .requiredMessageType = {DBusMessageType::NONE, DBusMessageType::NONE}},
    HeaderField{.decimalCode = HeaderFieldCode::PATH,
                .type = DBusTypeCodes::OBJECT_PATH,
                .requiredMessageType = {DBusMessageType::METHOD_CALL, DBusMessageType::SIGNAL}},
    HeaderField{
        .decimalCode = HeaderFieldCode::INTERFACE, .type = DBusTypeCodes::STRING, .requiredMessageType = {DBusMessageType::SIGNAL, DBusMessageType::NONE}},
    HeaderField{
        .decimalCode = HeaderFieldCode::MEMBER, .type = DBusTypeCodes::STRING, .requiredMessageType = {DBusMessageType::METHOD_CALL, DBusMessageType::SIGNAL}},
    HeaderField{
        .decimalCode = HeaderFieldCode::ERROR_NAME, .type = DBusTypeCodes::STRING, .requiredMessageType = {DBusMessageType::ERROR, DBusMessageType::NONE}},
    HeaderField{.decimalCode = HeaderFieldCode::REPLY_SERIAL,
                .type = DBusTypeCodes::UINT32,
                .requiredMessageType = {DBusMessageType::ERROR, DBusMessageType::METHOD_RETURN}},
    HeaderField{.decimalCode = HeaderFieldCode::DESTINATION,
                .type = DBusTypeCodes::STRING,
                .requiredMessageType = {DBusMessageType::OPTIONAL, DBusMessageType::OPTIONAL}},
    HeaderField{
        .decimalCode = HeaderFieldCode::SENDER, .type = DBusTypeCodes::STRING, .requiredMessageType = {DBusMessageType::OPTIONAL, DBusMessageType::OPTIONAL}},
    HeaderField{.decimalCode = HeaderFieldCode::SIGNATURE,
                .type = DBusTypeCodes::SIGNATURE,
                .requiredMessageType = {DBusMessageType::OPTIONAL, DBusMessageType::OPTIONAL}},
    HeaderField{
        .decimalCode = HeaderFieldCode::UNIX_FDS, .type = DBusTypeCodes::UINT32, .requiredMessageType = {DBusMessageType::OPTIONAL, DBusMessageType::OPTIONAL}},
};

template <IsDBusBasicFixedType T>
void MarshalBasicFixedType(T const& value, std::vector<byte>& dbusType)
{
  // Fixed Type
  if constexpr (std::is_same_v<T, bool>)
  {
    // Booleans are marshalled as uint32_t
    for (uint8_t i{}; i < 4; ++i)
    {
      uint32_t boolConvertedVal{static_cast<uint32_t>(value ? 1 : 0)};
      dbusType.push_back(*(static_cast<byte*>(static_cast<void*>(&boolConvertedVal)) + i));
    }
  }
  else
  {
    for (uint8_t i{}; i < sizeof(T); ++i)
    {
      dbusType.push_back(*(static_cast<byte const*>(static_cast<void const*>(&value)) + i));
    }
  }
}

template <IsDBusBasicStringlikeType T>
void MarshalBasicStringlikeType(T const& value, std::vector<byte>& dbusType)
{
  std::string const str{std::string{value}};

  if constexpr (IsString<T> || std::is_same_v<T, ObjectPath>)
  {
    // Encode the length
    MarshalBasicFixedType(static_cast<uint32_t>(str.size()), dbusType);
  }
  else
  {
    // Encode the length as a uint8_t
    MarshalBasicFixedType(static_cast<uint8_t>(str.size()), dbusType);
  }

  // [TODO]: Handle UTF-8
  for (unsigned char c : str)
  {
    dbusType.push_back(c);
  }

  dbusType.push_back('\0');
}

template <IsDBusBasicType T>
void MarshalBasicType(T const& value, std::vector<byte>& dbusType)
{
  if constexpr (IsDBusBasicFixedType<T>)
  {
    MarshalBasicFixedType(value, dbusType);
  }
  else
  {
    MarshalBasicStringlikeType(value, dbusType);
  }
}

template <IsDBusArray T>
void MarshalDBusArray(T const& value, std::vector<byte>& dbusType)
{
  using ContainedType = typename T::value_type;

  uint32_t size{};
  GetSizeOfDBusType(value, size);
  if (size >= 2 << 26)
  {
    throw std::length_error{"DBus Arrays cannot exceed a size of 64 MiB"};
  }

  // First we marshal a uint32_t fiving the length of the array (in bytes), followed by padding to the array's element type boundary
  MarshalBasicFixedType(size, dbusType);

  // Now we must find out the alignment of our DBus type.
  uint8_t const alignment = GetAlignmentOfDBusType<ContainedType>();

  ApplyPadding(dbusType, alignment);

  for (auto const& [index, elem] : std::views::enumerate(value))
  {
    MarshalDBusTypeImpl(elem, dbusType);
    if (index < value.size() - 1)
    {
      // Not last element, so add padding if required
      ApplyPadding(dbusType, alignment);
    }
  }
}

template <IsDBusStruct T, size_t I, size_t MaxI>
void MarshalDBusStruct(T const& value, std::vector<byte>& dbusType)
{
  using ElemType = typename std::tuple_element_t<I, T>;

  MarshalDBusTypeImpl<ElemType>(std::get<I>(value), dbusType);

  if constexpr (I < MaxI - 1)
  {
    using NextElemType = typename std::tuple_element_t<I + 1, T>;
    if constexpr (IsDBusMap<NextElemType>)
    {
      ApplyPadding(dbusType, GetAlignmentOfDBusType<uint32_t>());
    }
    else
    {
      ApplyPadding(dbusType, GetAlignmentOfDBusType<NextElemType>());
    }
  }
}

template <IsDBusStruct T>
void MarshalDBusStruct(T const& value, std::vector<byte>& dbusType)
{
  constexpr size_t structSize{std::tuple_size_v<T>};

  [&dbusType, &value]<size_t... Is>(std::index_sequence<Is...>)
  { (MarshalDBusStruct<T, Is, structSize>(value, dbusType), ...); }(std::make_index_sequence<structSize>{});
}

template <IsDBusType T>
void MarshalDBusMap(T const& value, std::vector<byte>& dbusType)
{
  // DBus dictionaries are defined as an Array of Dictionary Entries which are just Structs.
  // Therefore they're marshalled very to Arrays of Structs

  // DBus Spec requires that the key of a map must be a DBus Basic Type
  static_assert(IsDBusBasicType<typename T::key_type>, "DBus Maps can only have DBus Basic Types as keys");

  uint8_t const alignment{GetAlignmentOfDBusType<T>()};
  uint32_t size{};
  GetSizeOfDBusType(value, size);

  // First we marshal a uint32_t fiving the length of the array (in bytes), followed by padding to the array's element type boundary
  MarshalBasicFixedType(size - alignment, dbusType);

  ApplyPadding(dbusType, alignment);

  for (auto it{value.cbegin()}; it != value.cend(); ++it)
  {
    MarshalDBusTypeImpl(it->first, dbusType);

    if constexpr (IsDBusMap<typename T::mapped_type>)
    {
      // uint32_t because maps are arrays so pad to the array.
      ApplyPadding(dbusType, GetAlignmentOfDBusType<uint32_t>());
    }
    else
    {
      ApplyPadding(dbusType, GetAlignmentOfDBusType<typename T::mapped_type>());
    }
    MarshalDBusTypeImpl(it->second, dbusType);

    if (std::distance(it, value.cend()) > 1)
    {
      ApplyPadding(dbusType, alignment);
    }
  }

  // Make sure we pad even if our map is empty
  if (value.empty())
  {
    ApplyPadding(dbusType, alignment);
  }
}

template <IsDBusVariant T>
void MarshalDBusVariant(T const& value, std::vector<byte>& dbusType)
{
  value.MarshalData(dbusType);
}

template <IsDBusContainer T>
void MarshalDBusContainer(T const& value, std::vector<byte>& dbusType)
{
  if constexpr (IsDBusArray<T>)
  {
    MarshalDBusArray(value, dbusType);
  }
  else if constexpr (IsDBusStruct<T>)
  {
    MarshalDBusStruct(value, dbusType);
  }
  else if constexpr (IsDBusMap<T>)
  {
    MarshalDBusMap(value, dbusType);
  }
  else if constexpr (IsDBusVariant<T>)
  {
    MarshalDBusVariant(value, dbusType);
  }
  else
  {
    std::unreachable();
  }
}

template <IsDBusType T>
void MarshalDBusTypeImpl(T const& value, std::vector<byte>& dbusType)
{
  if constexpr (IsDBusBasicType<T>)
  {
    MarshalBasicType(value, dbusType);
  }
  else if constexpr (IsDBusContainer<T>)
  {
    MarshalDBusContainer(value, dbusType);
  }
  else
  {
    std::unreachable();
  }
}

template <IsDBusType T>
std::vector<byte> MarshalDBusType(T const& value)
{
  std::vector<byte> dbusType{};
  uint32_t size{};
  GetSizeOfDBusType(value, size);
  dbusType.reserve(size);

  MarshalDBusTypeImpl(value, dbusType);
  return dbusType;
}

template <IsDBusBasicType T>
T UnmarshalDBusBasicType(std::vector<byte> const& dbusType, char const signature)
{
}

template <IsDBusType T>
T UnmarshalDBusType(std::vector<byte> const& dbusType, std::string signature)  // Copy signature so we can modify it
{
  if constexpr (IsDBusBasicType<T>)
  {
    if (!IsDBusBasicTypeCode(signature[0]))
    {
      throw std::runtime_error{std::format("Type {} and Signature {} do not match", "T", signature)};
    }
    return UnmarshalDBusBasicType<T>(dbusType, signature[0]);
  }
}

inline void UnmarshalDBusMessage(std::vector<byte> const& dbusMessage)
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
}
