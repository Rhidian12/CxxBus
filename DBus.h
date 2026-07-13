#pragma once

#include <sys/types.h>

#include <algorithm>
#include <climits>
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

#include "ConstexprTypeName.h"

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

inline bool IsDBusContainerTypeCode(unsigned char c)
{
  switch (static_cast<DBusTypeCodes>(c))
  {
    case DBusTypeCodes::ARRAY:
    case DBusTypeCodes::STRUCT_BEGIN:
    case DBusTypeCodes::STRUCT_END:
    case DBusTypeCodes::DICT_BEGIN:
    case DBusTypeCodes::DICT_END:
    case DBusTypeCodes::VARIANT:
      return true;
    default:
      return false;
  }
}

inline bool IsDBusTypeCode(unsigned char c)
{
  return IsDBusBasicTypeCode(c) || IsDBusContainerTypeCode(c);
}

inline bool IsDBusTypeCode(std::string const & str)
{
  for (unsigned char c : str)
  {
    if (!IsDBusTypeCode(c))
    {
      return false;
    }
  }

  return true;
}

inline bool AreDBusTypeCodeBracketsEven(std::string const& str)
{
  int32_t counter{};

  for (unsigned char c : str)
  {
    switch (static_cast<DBusTypeCodes>(c))
    {
      case DBusTypeCodes::DICT_BEGIN:
      case DBusTypeCodes::STRUCT_BEGIN:
        ++counter;
        break;
      case DBusTypeCodes::STRUCT_END:
      case DBusTypeCodes::DICT_END:
        --counter;
        break;
      default:
        break;
    }
  }

  return counter == 0;
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
class DeserializedVariant;
template <typename... Ts>
class MultipleCompleteTypes;

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
inline constexpr bool IsDBusBasicFixedType_v =
    std::disjunction_v<std::is_same<T, uint8_t>, std::is_same<T, bool>,
                        std::is_same<T, int16_t>, std::is_same<T, uint16_t>,
                        std::is_same<T, int32_t>, std::is_same<T, uint32_t>,
                        std::is_same<T, int64_t>, std::is_same<T, uint64_t>,
                        std::is_same<T, float>, std::is_same<T, double>>;

template <typename T>
concept IsDBusBasicFixedType = IsDBusBasicFixedType_v<T>;

template <typename T>
concept IsString = std::same_as<T, std::string> || std::same_as<T, std::string_view>;

template <typename T>
concept IsDBusBasicStringlikeType = IsString<T> || std::same_as<T, ObjectPath> || std::same_as<T, Signature>;

template <typename T>
concept IsDBusMultipleCompleteTypes = IsSpecialisation<T, MultipleCompleteTypes>;

template <typename T>
concept IsDBusBasicType = IsDBusBasicFixedType<T> || IsDBusBasicStringlikeType<T> || IsDBusMultipleCompleteTypes<T>;

template <typename T>
concept IsDBusArray = IsSpecialisation<T, std::vector>;

template <typename T>
concept IsDBusStruct = IsSpecialisation<T, std::tuple>;

template <typename T>
concept IsDBusVariant = std::is_same_v<T, Variant>;

template <typename T>
concept IsDBusDeserializedVariant = IsSpecialisation<T, DeserializedVariant>;

template <typename T>
concept IsDBusMap = IsSpecialisation<T, std::map>;

template <typename T>
concept IsDBusContainer = IsDBusArray<T> || IsDBusMap<T> || IsDBusStruct<T> || IsDBusVariant<T> || IsDBusDeserializedVariant<T>;

template <typename T>
concept IsSerializableDBusType =
    IsDBusBasicType<std::remove_cvref_t<T>> || IsDBusContainer<std::remove_cvref_t<T>> && !IsDBusDeserializedVariant<std::remove_cvref_t<T>>;

template <typename T>
concept IsDeserializableDBusType = IsDBusBasicType<std::remove_cvref_t<T>> || IsDBusContainer<std::remove_cvref_t<T>> && !IsDBusVariant<std::remove_cvref_t<T>>;

template <typename T>
concept IsDBusType = IsSerializableDBusType<std::remove_cvref_t<T>> || IsDeserializableDBusType<std::remove_cvref_t<T>>;

template <IsDBusType T>
constexpr std::string GetTypeSignature()
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
  else if constexpr (IsDBusVariant<T> || IsDBusDeserializedVariant<T>)
  {
    return "v";
  }
  else if constexpr (IsDBusMap<T>)
  {
    return std::string{"a{"} + GetTypeSignature<typename T::key_type>() + GetTypeSignature<typename T::mapped_type>() + "}";
  }
  else if constexpr (IsDBusMultipleCompleteTypes<T>)
  {
    return std::string{[]<size_t... Is>(std::index_sequence<Is...>) { return (GetTypeSignature<std::tuple_element_t<Is, typename T::type>>() + ...); }(
                           std::make_index_sequence<std::tuple_size_v<typename T::type>>{})};
  }
}

inline void ApplyPadding(std::vector<byte>& bytes, uint8_t alignment)
{
  uint32_t const result{static_cast<uint32_t>(bytes.size()) % alignment};
  if (result == 0) return;

  bytes.append_range(std::vector<byte>(static_cast<uint8_t>(alignment - result), '\0'));
}

inline void AddPaddingToSize(uint32_t& size, uint8_t alignment)
{
  uint32_t const result{size % alignment};
  if (result == 0) return;

  size += alignment - result;
}

inline void SkipPadding(uint32_t& arrPointer, uint8_t alignment)
{
  uint32_t const result{arrPointer % alignment};
  if (result == 0) return;

  arrPointer += alignment - result;
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

  auto operator<=>(ObjectPath const&) const noexcept = default;
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

  std::string const& GetSignature() const
  {
    return m_signature;
  }

  // Get the alignment of the contained signature
  uint8_t GetAlignmentOfSignature() const
  {
    switch (static_cast<DBusTypeCodes>(m_signature[0]))
    {
      case DBusTypeCodes::BYTE:
      case DBusTypeCodes::SIGNATURE:
      case DBusTypeCodes::VARIANT:  // Alignment of Signature
        return 1;
      case DBusTypeCodes::INT16:
      case DBusTypeCodes::UINT16:
        return 2;
      case DBusTypeCodes::BOOLEAN:
      case DBusTypeCodes::UNIX_FD:
      case DBusTypeCodes::INT32:
      case DBusTypeCodes::UINT32:
      case DBusTypeCodes::STRING:
      case DBusTypeCodes::OBJECT_PATH:
      case DBusTypeCodes::ARRAY:
        return 4;
      case DBusTypeCodes::INT64:
      case DBusTypeCodes::UINT64:
      case DBusTypeCodes::DOUBLE:
      case DBusTypeCodes::STRUCT_BEGIN:
        return 8;
      case DBusTypeCodes::STRUCT:
      case DBusTypeCodes::STRUCT_END:
      case DBusTypeCodes::DICT:
      case DBusTypeCodes::DICT_BEGIN:
      case DBusTypeCodes::DICT_END:
      case DBusTypeCodes::INVALID:
        std::unreachable();
    }
  }

  auto operator<=>(Signature const&) const noexcept = default;
};

template <typename... Ts>
class MultipleCompleteTypes
{
 private:
  std::tuple<Ts...> m_types;

 public:
  using type = std::tuple<Ts...>;

 public:
  template <typename... Us>
  MultipleCompleteTypes(Us&&... types)
    : m_types(std::forward<Us>(types)...)
  {
  }

  std::tuple<Ts...> const& GetTypes() const
  {
    return m_types;
  }

  template<size_t I>
  auto GetType() const
  {
    return std::get<I>(m_types);
  }
};

template <IsSerializableDBusType T>
constexpr uint8_t GetAlignmentOfDBusType()
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

template <IsSerializableDBusType T>
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
  template <IsSerializableDBusType T>
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

template <typename T>
class DeserializedVariant
{
  static_assert(IsDeserializableDBusType<T>, "DeserializedVariant only supports types that can be deserialized from DBus types");

 private:
  T m_data;
  Signature m_signature;

 public:
  using type = T;

 public:
  template <IsDeserializableDBusType U = T>
    requires(!IsDBusVariant<U>)
  DeserializedVariant(U&& value, Signature const& signature)
    : m_data{std::forward<U>(value)}
    , m_signature(signature)
  {
  }

  DeserializedVariant(DeserializedVariant const& other) noexcept = default;
  DeserializedVariant(DeserializedVariant&& other) noexcept = default;
  DeserializedVariant& operator=(DeserializedVariant const& other) noexcept = default;
  DeserializedVariant& operator=(DeserializedVariant&& other) noexcept = default;

  template <typename Self>
  auto&& Get(this Self&& self)
  {
    return std::forward<Self>(self).m_data;
  }

  Signature const& GetSignature() const
  {
    return m_signature;
  }
};

template <IsSerializableDBusType T, size_t I, size_t MaxI>
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
    else if constexpr(IsDBusDeserializedVariant<T>)
    {
      GetSizeOfDBusType<Signature>(value.GetSignature(), size);
      AddPaddingToSize(size, GetAlignmentOfDBusType<typename T::type>());
      size += GetSizeOfDBusType(value.Get(), size);
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

template <IsDBusMultipleCompleteTypes T, size_t I, size_t MaxI>
void MarshalBasicMultipleCompleteTypes(T const& value, std::vector<byte>& dbusType)
{
  using ElemType = typename std::tuple_element_t<I, typename T::type>;

  MarshalDBusTypeImpl<ElemType>(std::get<I>(value.GetTypes()), dbusType);

  if constexpr (I < MaxI - 1)
  {
    using NextElemType = typename std::tuple_element_t<I + 1, typename T::type>;
    ApplyPadding(dbusType, GetAlignmentOfDBusType<NextElemType>());
  }
}

template <IsDBusMultipleCompleteTypes T>
void MarshalBasicMultipleCompleteTypes(T const& value, std::vector<byte>& dbusType)
{
  [&dbusType, &value]<size_t... Is>(std::index_sequence<Is...>)
  {
    (MarshalBasicMultipleCompleteTypes<T, Is, std::tuple_size_v<typename T::type>>(value, dbusType), ...);
  }(std::make_index_sequence<std::tuple_size_v<typename T::type>>{});
}

template <IsDBusBasicType T>
void MarshalBasicType(T const& value, std::vector<byte>& dbusType)
{
  if constexpr (IsDBusBasicFixedType<T>)
  {
    MarshalBasicFixedType(value, dbusType);
  }
  else if constexpr (IsDBusBasicStringlikeType<T>)
  {
    MarshalBasicStringlikeType(value, dbusType);
  }
  else
  {
    MarshalBasicMultipleCompleteTypes(value, dbusType);
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

template <IsSerializableDBusType T>
void MarshalDBusMap(T const& value, std::vector<byte>& dbusType)
{
  // DBus dictionaries are defined as an Array of Dictionary Entries which are just Structs.
  // Therefore they're marshalled similar to Arrays of Structs

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

template <IsSerializableDBusType T>
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

template <IsSerializableDBusType T>
std::vector<byte> MarshalDBusType(T const& value)
{
  std::vector<byte> dbusType{};
  uint32_t size{};
  GetSizeOfDBusType(value, size);
  dbusType.reserve(size);

  MarshalDBusTypeImpl(value, dbusType);
  return dbusType;
}

class DBusDeserializationError : public std::runtime_error
{
 public:
  using std::runtime_error::runtime_error;
};

class DBusMalformedInputError : public std::runtime_error
{
 public:
  using std::runtime_error::runtime_error;
};

class DBusInvalidSignatureError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

template <IsDeserializableDBusType T>
T UnmarshalDBusTypeImpl(std::vector<byte> const& dbusType, uint32_t& arrPointer);

template <IsDBusBasicFixedType T>
T UnmarshalDBusBasicFixedType(std::vector<byte> const& dbusType, uint32_t& arrPointer)
{
  constexpr uint32_t minSize{std::is_same_v<T, bool> ? sizeof(uint32_t) : sizeof(T)};

  if (minSize > (dbusType.size() - arrPointer))
  {
    throw DBusMalformedInputError{std::format("Trying to deserialize {} but the incoming buffer (total size: {}) has only {} bytes remaining while we need {} bytes",
                                              ConstexprTypeName<T>(), dbusType.size(), dbusType.size() - arrPointer, minSize)};
  }

  T value{};
  if constexpr (std::is_same_v<T, bool>)
  {
    uint32_t boolValue{};
    std::copy(dbusType.begin() + arrPointer, dbusType.begin() + (arrPointer + sizeof(uint32_t)), reinterpret_cast<byte*>(&boolValue));
    // uint32_t const boolValue{static_cast<uint32_t>((dbusType[0] << 24) | (dbusType[1] << 16) | (dbusType[2] << 8) | (dbusType[3]))};
    value = boolValue == 1;

    arrPointer += sizeof(uint32_t);
  }
  else
  {
    std::copy(dbusType.begin() + arrPointer, dbusType.begin() + (arrPointer + sizeof(T)), reinterpret_cast<byte*>(&value));
    // static_assert(CHAR_BIT == 8, "We only support 8 bits per byte");
    // [&value, &dbusType]<size_t... Is>(std::index_sequence<Is...>)
    // {
    //   // * 8 because 8 bits per byte
    //   // - Is - 1 so we count down from <nr of bytes> to 0
    //   value = static_cast<T>(((static_cast<T>(dbusType[Is]) << ((sizeof(T) - Is - 1) * 8)) | ...));
    // }(std::make_index_sequence<sizeof(T)>{});

    arrPointer += sizeof(T);
  }

  return value;
}

template <IsDBusBasicStringlikeType T>
  requires(!std::is_same_v<T, std::string_view>)
T UnmarshalDBusBasicStringlikeType(std::vector<byte> const& dbusType, uint32_t& arrPointer)
{
  uint32_t strLength{};
  if constexpr (IsString<T> || std::is_same_v<T, ObjectPath>)
  {
    strLength = UnmarshalDBusBasicFixedType<uint32_t>(dbusType, arrPointer);
  }
  else
  {
    strLength = static_cast<uint32_t>(UnmarshalDBusBasicFixedType<uint8_t>(dbusType, arrPointer));
  }

  if (strLength >= (dbusType.size() - arrPointer))
  {
    throw DBusMalformedInputError{
        std::format("Trying to deserialize {} with a claimed {} byte length, but the incoming buffer (total size: {}) has only {} bytes remaining",
                    ConstexprTypeName<T>(), strLength, dbusType.size(), dbusType.size() - arrPointer)};
  }

  std::string str{};
  str.reserve(strLength);

  for (uint32_t i{}; i < strLength; ++i)
  {
    str.push_back(static_cast<unsigned char>(dbusType[arrPointer++]));
  }

  // + 1 to also skip the null terminator
  ++arrPointer;

  if constexpr (IsString<T>)
  {
    return str;
  }
  else
  {
    return T{str};
  }
}

template <IsDBusMultipleCompleteTypes T, size_t I, size_t MaxI>
auto UnmarshalDBusBasicMultipleCompleteTypes(std::vector<byte> const& dbusType, uint32_t& arrPointer)
{
  using ElemType = typename std::tuple_element_t<I, typename T::type>;

  ElemType elem = UnmarshalDBusTypeImpl<ElemType>(dbusType, arrPointer);

  if constexpr (I < MaxI - 1)
  {
    using NextElemType = typename std::tuple_element_t<I + 1, typename T::type>;
    SkipPadding(arrPointer, GetAlignmentOfDBusType<NextElemType>());
  }

  return elem;
}

template <IsDBusMultipleCompleteTypes T>
T UnmarshalDBusBasicMultipleCompleteTypes(std::vector<byte> const& dbusType, uint32_t& arrPointer)
{
  return [&dbusType, &arrPointer]<size_t... Is>(std::index_sequence<Is...>) -> T
  {
    return std::make_tuple(UnmarshalDBusBasicMultipleCompleteTypes<T, Is, std::tuple_size_v<typename T::type>>(dbusType, arrPointer)...);
  }(std::make_index_sequence<std::tuple_size_v<typename T::type>>{});
}

template <IsDBusBasicType T>
T UnmarshalDBusBasicType(std::vector<byte> const& dbusType, uint32_t& arrPointer)
{
  if constexpr (IsDBusBasicFixedType<T>)
  {
    return UnmarshalDBusBasicFixedType<T>(dbusType, arrPointer);
  }
  else if constexpr (IsDBusBasicStringlikeType<T>)
  {
    return UnmarshalDBusBasicStringlikeType<T>(dbusType, arrPointer);
  }
  else
  {
    return UnmarshalDBusBasicMultipleCompleteTypes<T>(dbusType, arrPointer);
  }
}

template <IsDBusArray T>
T UnmarshalDBusArray(std::vector<byte> const& dbusType, uint32_t& arrPointer)
{
  uint32_t arrLength{UnmarshalDBusBasicFixedType<uint32_t>(dbusType, arrPointer)};

  if (arrLength >= 2 << 26)
  {
    throw std::length_error{"DBus Arrays cannot exceed a size of 64 MiB"};
  }

  // 'arrLength' will be about multiple strings, and therefore calculating our array length makes very little sense as it is spread over multiple strings
  if constexpr (!IsDBusBasicStringlikeType<typename T::value_type>)
  {
    // We marshal the length of the array in bytes, so convert that back to nr of elements we serialized
    arrLength /= sizeof(typename T::value_type);
  }

  // Remove any potential padding
  SkipPadding(arrPointer, GetAlignmentOfDBusType<typename T::value_type>());

  T vec{};

  if constexpr (IsDBusBasicStringlikeType<typename T::value_type>)
  {
    // For strings we'll just reserve an arbitrary amount of space
    vec.reserve(8);
  }
  else
  {
    vec.reserve(arrLength);
  }

  if constexpr (IsDBusBasicStringlikeType<typename T::value_type>)
  {
    // Since the only thing we know for strings is how long the entire array is, we just keep unmarshalling strings until we reach the end of the array
    uint32_t bytesRead{};
    while (bytesRead < arrLength)
    {
      if (arrPointer >= dbusType.size())
      {
        throw DBusMalformedInputError{std::format(
            "Trying to deserialize {} with a claimed {} byte length, but the incoming buffer (total size: {}) has only {} bytes remainings",
            ConstexprTypeName<T>(), arrLength, dbusType.size(), dbusType.size() - arrPointer)};
      }

      vec.push_back(UnmarshalDBusTypeImpl<typename T::value_type>(dbusType, arrPointer));
      bytesRead += sizeof(uint32_t) + vec.back().size() + 1;

      if (bytesRead < arrLength)
      {
        size_t const oldPointer{arrPointer};
        SkipPadding(arrPointer, GetAlignmentOfDBusType<typename T::value_type>());
        bytesRead += arrPointer - oldPointer;  // Remove any potential padding
      }
    }
  }
  else
  {
    for (uint32_t i{}; i < arrLength; ++i)
    {
      vec.push_back(UnmarshalDBusTypeImpl<typename T::value_type>(dbusType, arrPointer));

      SkipPadding(arrPointer, GetAlignmentOfDBusType<typename T::value_type>());
    }
  }

  return vec;
}

template <IsDBusStruct T, size_t I, size_t MaxI>
auto UnmarshalDBusStruct(std::vector<byte> const& dbusType, uint32_t& arrPointer)
{
  using ElemType = typename std::tuple_element_t<I, T>;

  ElemType elem = UnmarshalDBusTypeImpl<ElemType>(dbusType, arrPointer);

  if constexpr (I < MaxI - 1)
  {
    using NextElemType = typename std::tuple_element_t<I + 1, T>;
    if constexpr (IsDBusMap<NextElemType>)
    {
      SkipPadding(arrPointer, GetAlignmentOfDBusType<uint32_t>());
    }
    else
    {
      SkipPadding(arrPointer, GetAlignmentOfDBusType<NextElemType>());
    }
  }

  return elem;
}

template <IsDBusStruct T>
T UnmarshalDBusStruct(std::vector<byte> const& dbusType, uint32_t& arrPointer)
{
  return [&dbusType, &arrPointer]<size_t... Is>(std::index_sequence<Is...>) -> T
  { return std::make_tuple(UnmarshalDBusStruct<T, Is, std::tuple_size_v<T>>(dbusType, arrPointer)...); }(std::make_index_sequence<std::tuple_size_v<T>>{});
}

template <IsDBusMap T>
T UnmarshalDBusMap(std::vector<byte> const& dbusType, uint32_t& arrPointer)
{
  using KeyT = typename T::key_type;
  using MappedT = typename T::mapped_type;

  uint32_t const mapLength{UnmarshalDBusTypeImpl<uint32_t>(dbusType, arrPointer)};

  uint32_t oldArrPointer{arrPointer};
  // Remove the padding between the leading uint32_t and our DICT_ENTRY
  SkipPadding(arrPointer, GetAlignmentOfDBusType<T>());

  T map{};

  // It's hard to tell how many elements are in a map because of the padding requirements, so just read our map until we've read its full length
  if (mapLength == 0)
  {
    // When we have no elements in our map, we pad until the 8 byte boundary
    SkipPadding(arrPointer, GetAlignmentOfDBusType<T>());
  }
  else
  {
    uint32_t bytesRead{};
    while (bytesRead < mapLength)
    {
      oldArrPointer = arrPointer;
      KeyT const key{UnmarshalDBusTypeImpl<KeyT>(dbusType, arrPointer)};

      if constexpr (IsDBusMap<MappedT>)
      {
        // uint32_t because maps are arrays so alignment is of the array.
        SkipPadding(arrPointer, GetAlignmentOfDBusType<uint32_t>());
      }
      else
      {
        SkipPadding(arrPointer, GetAlignmentOfDBusType<MappedT>());
      }

      MappedT const value{UnmarshalDBusTypeImpl<MappedT>(dbusType, arrPointer)};

      map.emplace(key, value);

      // Update how much we read
      bytesRead += arrPointer - oldArrPointer;

      if (bytesRead < mapLength)
      {
        oldArrPointer = arrPointer;

        // DICT_ENTRY is a struct, so just take our T's alignment (which is guaranteed to be 8)
        AddPaddingToSize(arrPointer, GetAlignmentOfDBusType<T>());

        // Update how much we read
        bytesRead += arrPointer - oldArrPointer;
      }
    }
  }

  return map;
}

// class DBusParserError : public std::runtime_error
// {
// public:
//   using std::runtime_error::runtime_error;
// };

// struct TypeNode
// {
//   // Type of the node we parsed
//   DBusTypeCodes typeCode;

//   // If the type of this node is a container then we have children
//   std::vector<std::unique_ptr<TypeNode>> children;

//   TypeNode() = default;
//   TypeNode(DBusTypeCodes typeCode_, std::vector<std::unique_ptr<TypeNode>>&& children_)
//     : typeCode(typeCode_)
//     , children(std::move(children_))
//   {
//   }
//   TypeNode(TypeNode &&) noexcept = default;
//   TypeNode& operator=(TypeNode&&) noexcept = default;
// };

// std::optional<TypeNode> ParseSignature(std::string const & originalSignature, std::string& signature)
// {
//   TypeNode node;

//   char const poppedChar{signature.front()};
//   signature.erase(signature.begin(), signature.begin() + 1);

//   if (IsDBusBasicTypeCode(poppedChar))
//   {
//     node.typeCode = static_cast<DBusTypeCodes>(poppedChar);
//   }
//   else
//   {
//     DBusTypeCodes endNodeType{};
//     // Container type
//     switch (static_cast<DBusTypeCodes>(poppedChar))
//     {
//       case DBusTypeCodes::STRUCT_END:
//       case DBusTypeCodes::DICT_END:
//         return std::nullopt;
//       case DBusTypeCodes::STRUCT_BEGIN:
//         node.typeCode = DBusTypeCodes::STRUCT;
//         break;
//       case DBusTypeCodes::DICT_BEGIN:
//         node.typeCode = DBusTypeCodes::DICT;
//         break;
//       case DBusTypeCodes::ARRAY:
//         if (static_cast<DBusTypeCodes>(signature[0]) == DBusTypeCodes::DICT_BEGIN)
//         {
//           node.typeCode = DBusTypeCodes::DICT;

//           // Remove '{'
//           // We do this extra remove here because otherwise we'd get 2 TypeNode of type DICT, while we only have 1 DICT in the signature
//           signature.erase(signature.begin(), signature.begin() + 1);
//         }
//         else
//         {
//           node.typeCode = DBusTypeCodes::ARRAY;
//         }
//         break;
//       case DBusTypeCodes::VARIANT:
//         node.typeCode = DBusTypeCodes::VARIANT;
//         break;
//       default:
//         std::unreachable();
//     }

//     while (true)
//     {
//       if (std::optional<TypeNode> childNode{ParseSignature(originalSignature, signature)}; !childNode.has_value())
//       {
//         break;
//       }
//       else
//       {
//         node.children.emplace_back(std::make_unique<TypeNode>(childNode->typeCode, std::move(childNode->children)));
//       }
//     }

//     if (node.children.empty())
//     {
//       throw DBusParserError{std::format("Error while parsing signature '{}': Container has no elements which is illegal.", originalSignature)};
//     }
//   }

//   return node;
// }

template <IsDBusDeserializedVariant T>
T UnmarshalDBusVariant(std::vector<byte> const& dbusType, uint32_t& arrPointer)
{
  Signature const signature{UnmarshalDBusTypeImpl<Signature>(dbusType, arrPointer)};
  SkipPadding(arrPointer, signature.GetAlignmentOfSignature());

  if (GetTypeSignature<typename T::type>() != signature.GetSignature())
  {
    throw DBusDeserializationError{std::format("Cannot deserialize DBus Variant Signature '{}' into provided DeserializedVariant with signature '{}'",
                                               signature.GetSignature(), GetTypeSignature<typename T::type>())};
  }

  T deserializedVariant{UnmarshalDBusTypeImpl<typename T::type>(dbusType, arrPointer), signature};
  return deserializedVariant;
}

template <IsDBusContainer T>
T UnmarshalDBusContainer(std::vector<byte> const& dbusType, uint32_t& arrPointer)
{
  if constexpr (IsDBusArray<T>)
  {
    return UnmarshalDBusArray<T>(dbusType, arrPointer);
  }
  else if constexpr (IsDBusStruct<T>)
  {
    return UnmarshalDBusStruct<T>(dbusType, arrPointer);
  }
  else if constexpr (IsDBusMap<T>)
  {
    return UnmarshalDBusMap<T>(dbusType, arrPointer);
  }
  else if constexpr (IsDBusDeserializedVariant<T>)
  {
    return UnmarshalDBusVariant<T>(dbusType, arrPointer);
  }
}

template <IsDeserializableDBusType T>
T UnmarshalDBusTypeImpl(std::vector<byte> const& dbusType, uint32_t& arrPointer)
{
  if constexpr (IsDBusBasicType<T>)
  {
    return UnmarshalDBusBasicType<T>(dbusType, arrPointer);
  }
  else
  {
    return UnmarshalDBusContainer<T>(dbusType, arrPointer);
  }
}

template <IsDeserializableDBusType T>
T UnmarshalDBusType(std::vector<byte> dbusType, std::string const& signature)
{
  if (!IsDBusTypeCode(signature))
  {
    throw DBusInvalidSignatureError{std::format("Signature '{}' contains unknown DBus Type Codes.", signature)};
  }

  if (!AreDBusTypeCodeBracketsEven(signature))
  {
    throw DBusInvalidSignatureError{std::format("Signature '{}' is incorrect and contains an uneven amount of brackets", signature)};
  }

  if (signature != GetTypeSignature<T>())
  {
    throw DBusInvalidSignatureError{std::format("Type {} (signature: '{}') and Signature {} do not match.", ConstexprTypeName<T>(), GetTypeSignature<T>(), signature)};
  }

  uint32_t arrPointer{};
  T value = UnmarshalDBusTypeImpl<T>(dbusType, arrPointer);
  if (arrPointer != dbusType.size())
  {
    throw DBusMalformedInputError{std::format("Deserialized {} but the incoming buffer (total size: {}) has {} bytes remaining", ConstexprTypeName<T>(),
                                              dbusType.size(), dbusType.size() - arrPointer)};
  }
  return value;
}
