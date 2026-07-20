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
#include "DBusHelpers.h"
#include "DBusTypes.h"

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

template <IsDBusType T>
void MarshalDBusTypeImpl(T const& value, std::vector<byte>& dbusType);

template <IsDBusType T>
T UnmarshalDBusTypeImpl(std::vector<byte> const& dbusType, uint32_t& arrPointer);

template <IsDBusType T>
void GetSizeOfDBusType(T const& value, uint32_t& size);

struct InPlaceT
{
};
constexpr InPlaceT in_place{};

struct DeserializedVariantTag
{
};
constexpr DeserializedVariantTag deserialized_variant_tag{};

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

  struct DeserializedVariantData
  {
    Signature signature;
    std::vector<byte> data;
  };

  std::variant<VariantData, DeserializedVariantData, std::monostate> m_variantData;

 public:
  template <IsDBusType T>
    requires(!std::is_same_v<std::remove_cvref_t<T>, Variant>)
  explicit Variant(T&& value)
    : m_variantData{VariantData{
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
          }}}
  {
    GetSizeOfDBusType(value, std::get<VariantData>(m_variantData).dataSize);
  }

  // Wraps a Variant inside a Variant (nested/boxed variant)
  Variant(InPlaceT, Variant variant)
    : m_variantData{VariantData{.signature = GetTypeSignature<Variant>(),
                                .dataAlignment = GetAlignmentOfDBusType<Variant>(),
                                .data = std::unique_ptr<void, CustomDeleter>(new Variant(std::move(variant)),
                                                                             CustomDeleter{.deleter = [](void* data) { delete static_cast<Variant*>(data); }}),
                                .marshalDataFunc = [](void* data, std::vector<byte>& dbusType) { static_cast<Variant*>(data)->MarshalData(dbusType); },
                                .copyFunc =
                                    [](void* otherData)
                                {
                                  return std::unique_ptr<void, CustomDeleter>(new Variant(*static_cast<Variant*>(otherData)),
                                                                              CustomDeleter{.deleter = [](void* data) { delete static_cast<Variant*>(data); }});
                                }}}
  {
    GetSizeOfDBusType<Signature>(variant.GetSignature(), std::get<VariantData>(m_variantData).dataSize);
    std::get<VariantData>(m_variantData).dataSize += variant.GetDataSize();
  }

  Variant(DeserializedVariantTag, Signature signature, std::vector<byte> data)
    : m_variantData{DeserializedVariantData{.signature = std::move(signature), .data = std::move(data)}}
  {
  }

  Variant(Variant const& other)
    : m_variantData(std::monostate{})
  {
    if (std::holds_alternative<VariantData>(other.m_variantData))
    {
      VariantData const& data = std::get<VariantData>(other.m_variantData);
      m_variantData = VariantData{.signature = data.signature,
                                  .dataSize = data.dataSize,
                                  .dataAlignment = data.dataAlignment,
                                  .data = data.copyFunc(data.data.get()),
                                  .marshalDataFunc = data.marshalDataFunc,
                                  .copyFunc = data.copyFunc};
    }
    else if (std::holds_alternative<DeserializedVariantData>(other.m_variantData))
    {
      DeserializedVariantData const& data = std::get<DeserializedVariantData>(other.m_variantData);
      m_variantData = DeserializedVariantData{.signature = data.signature, .data = data.data};
    }
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
    if (std::holds_alternative<VariantData>(m_variantData))
    {
      return std::get<VariantData>(m_variantData).signature;
    }
    else if (std::holds_alternative<DeserializedVariantData>(m_variantData))
    {
      return std::get<DeserializedVariantData>(m_variantData).signature;
    }
    else
    {
      throw std::runtime_error{"Variant is in an invalid state"};
    }
  }
  uint32_t GetDataSize() const
  {
    if (std::holds_alternative<VariantData>(m_variantData))
    {
      return std::get<VariantData>(m_variantData).dataSize;
    }
    else if (std::holds_alternative<DeserializedVariantData>(m_variantData))
    {
      return std::get<DeserializedVariantData>(m_variantData).data.size();
    }
    else
    {
      throw std::runtime_error{"Variant is in an invalid state"};
    }
  }
  uint8_t GetDataAlignment() const
  {
    if (std::holds_alternative<VariantData>(m_variantData))
    {
      return std::get<VariantData>(m_variantData).dataAlignment;
    }
    else if (std::holds_alternative<DeserializedVariantData>(m_variantData))
    {
      return 1;  // Deserialized data alignment is 1
    }
    else
    {
      throw std::runtime_error{"Variant is in an invalid state"};
    }
  }

  void MarshalData(std::vector<byte>& dbusType) const
  {
    if (!std::holds_alternative<VariantData>(m_variantData))
    {
      throw std::runtime_error{"Cannot marshal a deserialized variant"};
    }

    VariantData const& data = std::get<VariantData>(m_variantData);

    // We marshal a variant by marshalling its signature followed by the data (with padding of course)
    // Add signature + padding to data type
    MarshalDBusTypeImpl(data.signature, dbusType);
    ApplyPadding(dbusType, data.dataAlignment);

    data.marshalDataFunc(data.data.get(), dbusType);
  }

  template <IsDBusType T>
  T UnmarshalData() const
  {
    if (!std::holds_alternative<DeserializedVariantData>(m_variantData))
    {
      throw std::runtime_error{"Cannot unmarshal a non-deserialized variant"};
    }

    DeserializedVariantData const& data = std::get<DeserializedVariantData>(m_variantData);

    if (GetTypeSignature<T>() != data.signature)
    {
      throw std::runtime_error{std::format("Type signature mismatch when unmarshalling variant. Variant contains {} but we're trying to deserialize {}",
                                           data.signature.GetSignature(), GetTypeSignature<T>())};
    }

    uint32_t arrPointer{};

    return UnmarshalDBusTypeImpl<T>(data.data, arrPointer);
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
      size += sizeof(uint8_t) + value.Size() + 1;
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
      GetSizeOfDBusType<Signature>(value.GetSignature(), size);
      AddPaddingToSize(size, value.GetDataAlignment());
      size += value.GetDataSize();
    }
    else
    {
      static_assert(false, "Unknown dbus container type");
    }
  }
}

inline uint32_t GetSizeOfDBusTypeBasedOnSignature(Signature const& signature, std::vector<byte> const& dbusType, uint32_t& arrPointer)
{
  switch (static_cast<DBusTypeCodes>(signature.GetSignature()[0]))
  {
    case DBusTypeCodes::BYTE:
      return sizeof(uint8_t);
    case DBusTypeCodes::BOOLEAN:
      return sizeof(bool);
    case DBusTypeCodes::INT16:
      return sizeof(int16_t);
    case DBusTypeCodes::UINT16:
      return sizeof(uint16_t);
    case DBusTypeCodes::INT32:
      return sizeof(int32_t);
    case DBusTypeCodes::UINT32:
      return sizeof(uint32_t);
    case DBusTypeCodes::INT64:
      return sizeof(int64_t);
    case DBusTypeCodes::UINT64:
      return sizeof(uint64_t);
    case DBusTypeCodes::DOUBLE:
      return sizeof(double);
    case DBusTypeCodes::STRING:
    case DBusTypeCodes::OBJECT_PATH:
    {
      // Length of string as u32 + actual length of string + '\0'
      uint32_t const length = UnmarshalDBusTypeImpl<uint32_t>(dbusType, arrPointer);
      arrPointer -= sizeof(uint32_t);  // Move back the pointer so we can simply skip over it in the main Unmarshal function
      return sizeof(uint32_t) + length + 1;
    }
    case DBusTypeCodes::SIGNATURE:
    {
      // Length of string as u8 + actual length of string + '\0'
      uint8_t const length = UnmarshalDBusTypeImpl<uint8_t>(dbusType, arrPointer);
      arrPointer -= sizeof(uint8_t);  // Move back the pointer so we can simply skip over it in the main Unmarshal function
      return sizeof(uint8_t) + length + 1;
    }
    case DBusTypeCodes::ARRAY:
    {
      // Length of array data in bytes
      uint32_t length = UnmarshalDBusTypeImpl<uint32_t>(dbusType, arrPointer);

      if (static_cast<DBusTypeCodes>(signature.GetSignature().at(1)) == DBusTypeCodes::DICT_BEGIN)
      {
        // Dictionary, we pad to 8-byte boundary, so we need to add padding to the size of the array data
        AddPaddingToSize(length, 8);
      }
      else
      {
        AddPaddingToSize(length, GetAlignmentOfSignature(Signature(std::string(1, signature.GetSignature().at(1)))));
      }

      arrPointer -= sizeof(uint32_t);  // Move back the pointer so we can simply skip over it in the main Unmarshal function
      return sizeof(uint32_t) + length;
    }
    case DBusTypeCodes::STRUCT_BEGIN:
    {
      uint32_t length = 0;
      for (size_t i = 1; i < signature.GetSignature().size() - 1; ++i)
      {
        Signature memberSignature(std::string(1, signature.GetSignature().at(i)));
        length += GetSizeOfDBusTypeBasedOnSignature(memberSignature, dbusType, arrPointer);
        if (i < signature.GetSignature().size() - 2)
        {
          AddPaddingToSize(length, GetAlignmentOfSignature(Signature(std::string(1, signature.GetSignature().at(i + 1)))));
        }
      }
      return length;
    }
    case DBusTypeCodes::VARIANT:
    {
      // Variant = Signature + Padding + Size of data
      Signature variantSignature = UnmarshalDBusTypeImpl<Signature>(dbusType, arrPointer);
      arrPointer -= sizeof(uint8_t) + variantSignature.Size() + 1;  // Move back the pointer so we can simply skip over it in the main Unmarshal function

      uint32_t length = GetSizeOfDBusTypeBasedOnSignature(variantSignature, dbusType, arrPointer);
      AddPaddingToSize(length, GetAlignmentOfSignature(variantSignature));
      return sizeof(uint8_t) + variantSignature.Size() + 1 + length;
    }
    default:
      throw std::runtime_error{"Unsupported type for size calculation based on signature"};
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

template <IsDBusType T>
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

template <IsDBusBasicFixedType T>
T UnmarshalDBusBasicFixedType(std::vector<byte> const& dbusType, uint32_t& arrPointer)
{
  constexpr uint32_t minSize{std::is_same_v<T, bool> ? sizeof(uint32_t) : sizeof(T)};

  if (minSize > (dbusType.size() - arrPointer))
  {
    throw DBusMalformedInputError{
        std::format("Trying to deserialize {} but the incoming buffer (total size: {}) has only {} bytes remaining while we need {} bytes",
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
  uint32_t const arrLength{UnmarshalDBusBasicFixedType<uint32_t>(dbusType, arrPointer)};

  if (arrLength >= 2 << 26)
  {
    throw std::length_error{"DBus Arrays cannot exceed a size of 64 MiB"};
  }

  // Remove any potential padding
  SkipPadding(arrPointer, GetAlignmentOfDBusType<typename T::value_type>());

  T vec{};

  // It's pretty hard to calculate exactly how many elements are in the array, so just reserve some arbitrary amount of space
  // [TODO]: Improve this
  vec.reserve(8);

  // Since the only thing we know for strings is how long the entire array is, we just keep unmarshalling strings until we reach the end of the array
  uint32_t bytesRead{};
  while (bytesRead < arrLength)
  {
    if (arrPointer >= dbusType.size())
    {
      throw DBusMalformedInputError{
          std::format("Trying to deserialize {} with a claimed {} byte length, but the incoming buffer (total size: {}) has only {} bytes remainings",
                      ConstexprTypeName<T>(), arrLength, dbusType.size(), dbusType.size() - arrPointer)};
    }

    vec.push_back(UnmarshalDBusTypeImpl<typename T::value_type>(dbusType, arrPointer));
    GetSizeOfDBusType(vec.back(), bytesRead);
    // if constexpr (IsDBusBasicStringlikeType<T>)
    // {
    //   bytesRead += sizeof(uint32_t) + vec.back().size() + 1;
    // }
    // else
    // {
    // }

    if (bytesRead < arrLength)
    {
      size_t const oldPointer{arrPointer};
      SkipPadding(arrPointer, GetAlignmentOfDBusType<typename T::value_type>());
      bytesRead += arrPointer - oldPointer;  // Remove any potential padding
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

template <IsDBusVariant T>
T UnmarshalDBusVariant(std::vector<byte> const& dbusType, uint32_t& arrPointer)
{
  Signature const signature{UnmarshalDBusTypeImpl<Signature>(dbusType, arrPointer)};
  SkipPadding(arrPointer, signature.GetAlignmentOfSignature());

  uint32_t amountOfData{};
  GetSizeOfDBusType(signature, amountOfData);

  T deserializedVariant{deserialized_variant_tag, signature,
                        std::ranges::to<std::vector>(dbusType | std::views::drop(arrPointer) |
                                                     std::views::take(GetSizeOfDBusTypeBasedOnSignature(signature, dbusType, arrPointer)))};
  arrPointer += GetSizeOfDBusTypeBasedOnSignature(signature, dbusType, arrPointer);

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
  else if constexpr (IsDBusVariant<T>)
  {
    return UnmarshalDBusVariant<T>(dbusType, arrPointer);
  }
}

template <IsDBusType T>
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

template <IsDBusType T>
T UnmarshalDBusType(std::vector<byte> dbusType, std::string const& signature, uint32_t& arrPointer)
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
    throw DBusInvalidSignatureError{
        std::format("Type {} (signature: '{}') and Signature {} do not match.", ConstexprTypeName<T>(), GetTypeSignature<T>(), signature)};
  }

  return UnmarshalDBusTypeImpl<T>(dbusType, arrPointer);
}

template <IsDBusType T>
T UnmarshalDBusType(std::vector<byte> dbusType, std::string const& signature)
{
  uint32_t arrPointer{};
  T value{UnmarshalDBusType<T>(dbusType, signature, arrPointer)};

  if (arrPointer != dbusType.size())
  {
    throw DBusMalformedInputError{std::format("Deserialized {} but the incoming buffer (total size: {}) has {} bytes remaining", ConstexprTypeName<T>(),
                                              dbusType.size(), dbusType.size() - arrPointer)};
  }

  return value;
}
