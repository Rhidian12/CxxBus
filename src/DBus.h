// MIT License
//
// Copyright (c) 2026 Rhidian De Wit
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <sys/types.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "ConstexprTypeName.h"
#include "DBusConcepts.h"
#include "DBusHelpers.h"
#include "DBusTypes.h"
#include "Log.h"

namespace cxxbus
{
  inline void ApplyPadding(std::vector<byte>& bytes, uint8_t alignment)
  {
    uint32_t const result{static_cast<uint32_t>(bytes.size()) % alignment};
    if (result == 0) return;

#if __cpp_lib_containers_ranges
    bytes.append_range(std::vector<byte>(static_cast<uint8_t>(alignment - result), '\0'));
#else
    bytes.insert(bytes.end(), static_cast<uint8_t>(alignment - result), '\0');
#endif
  }

  inline uint32_t AddPaddingToSize(uint32_t& size, uint8_t alignment)
  {
    uint32_t const result{size % alignment};
    if (result == 0) return 0;

    size += alignment - result;

    return alignment - result;
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
  T UnmarshalDBusTypeImpl(std::span<byte const> dbusType, uint32_t& arrPointer);

  struct InPlaceT
  {
  };
  constexpr InPlaceT in_place{};

  struct DeserializedVariantTag
  {
  };
  constexpr DeserializedVariantTag deserialized_variant_tag{};

  class VariantUnmarshalError : public std::runtime_error
  {
   public:
    using std::runtime_error::runtime_error;
  };

  struct VariantVTable
  {
    void (*marshalDataFunc)(void const*, std::vector<byte>&);
  };

  template <typename T, typename TOVerride = std::remove_cvref_t<T>>
  inline static constexpr VariantVTable vTableInstance{
      .marshalDataFunc = [](void const* data, std::vector<byte>& dbusType)
      {
        if constexpr (std::is_pointer_v<std::remove_cvref_t<T>>)
        {
          TOVerride castData{static_cast<std::remove_reference_t<T>>(data)};
          MarshalDBusTypeImpl(castData, dbusType);
        }
        else
        {
          TOVerride const* castData{static_cast<std::remove_reference_t<T> const*>(data)};
          MarshalDBusTypeImpl(*castData, dbusType);
        }
      }};

  inline uint32_t RoundUp(uint32_t number, uint32_t multiple)
  {
    return ((number + multiple - 1) / multiple) * multiple;
  }

  class Variant
  {
   public:
    inline constexpr static uint32_t SMALL_BUFFER_SIZE = 32;

   private:
    struct VariantData
    {
      Signature signature;
      uint8_t dataAlignment;
      std::variant<std::shared_ptr<void>, std::array<byte, SMALL_BUFFER_SIZE>> data;
      VariantVTable const* vTable;

      bool operator==(VariantData const& other) const noexcept
      {
        return signature == other.signature && dataAlignment == other.dataAlignment && data == other.data;
      }
    };

    struct DeserializedVariantData
    {
      Signature signature;
      std::variant<std::shared_ptr<std::vector<byte>>, std::array<byte, SMALL_BUFFER_SIZE>> data;

      auto operator<=>(DeserializedVariantData const&) const noexcept = default;
    };

    std::variant<VariantData, DeserializedVariantData, std::monostate> m_variantData;

   public:
    Variant()
      : m_variantData{std::monostate{}}
    {
    }

    template <IsDBusType T>
    static Variant Create(T&& value)
    {
      Variant variant;
      if constexpr (std::is_trivially_copyable_v<std::remove_cvref_t<T>>)
      {
        if (sizeof(std::remove_cvref_t<T>) <= SMALL_BUFFER_SIZE)
        {
          std::array<byte, SMALL_BUFFER_SIZE> buff{};
          std::memcpy(buff.data(), &value, sizeof(std::remove_cvref_t<T>));
          variant.m_variantData.emplace<VariantData>(std::string{GetTypeSignature<std::remove_cvref_t<T>>()},
                                                     GetAlignmentOfDBusType<std::remove_cvref_t<T>>(), std::move(buff),
                                                     &vTableInstance<T>);

          return variant;
        }
      }
      else if constexpr (IsDBusBasicStringlikeType<T>)
      {
        uint32_t length{};
        if constexpr (IsRawStringLiteral<T>)
        {
          // 'strlen()' is not safe, but we are depending on the user to pass null-terminated C strings if they pass C
          // strings
          length = strlen(value);
        }
        else
        {
          length = value.size();
        }

        // '- 1' because we need to store the null terminator
        if (length <= SMALL_BUFFER_SIZE - 1)
        {
          std::array<byte, SMALL_BUFFER_SIZE> buff{};
          if constexpr (IsRawStringLiteral<T>)
          {
            std::memcpy(buff.data(), value, length);
          }
          else
          {
            std::memcpy(buff.data(), value.data(), length);
          }

          variant.m_variantData.emplace<VariantData>(std::string{GetTypeSignature<std::remove_cvref_t<T>>()},
                                                     GetAlignmentOfDBusType<std::remove_cvref_t<T>>(), std::move(buff),
                                                     &vTableInstance<char const*, std::remove_cvref_t<T>>);

          return variant;
        }
      }

      // Nested variants don't get optimized
      variant.m_variantData.emplace<VariantData>(
          std::string{GetTypeSignature<std::remove_cvref_t<T>>()}, GetAlignmentOfDBusType<std::remove_cvref_t<T>>(),
          std::make_shared<std::decay_t<T>>(std::forward<T>(value)), &vTableInstance<T>);
      return variant;
    }

    Variant(DeserializedVariantTag, Signature&& signature, std::vector<byte>&& data)
      : m_variantData{std::monostate{}}
    {
      m_variantData.emplace<DeserializedVariantData>(std::move(signature),
                                                     std::make_shared<std::vector<byte>>(std::move(data)));
    }

    Variant(DeserializedVariantTag, Signature&& signature, std::array<byte, SMALL_BUFFER_SIZE>&& data)
      : m_variantData{std::monostate{}}
    {
      m_variantData.emplace<DeserializedVariantData>(std::move(signature), std::move(data));
    }

    Variant(Variant const& other)
      : m_variantData(std::monostate{})
    {
      if (std::holds_alternative<VariantData>(other.m_variantData))
      {
        VariantData const& data = std::get<VariantData>(other.m_variantData);
        m_variantData.emplace<VariantData>(data.signature, data.dataAlignment, data.data, data.vTable);
      }
      else if (std::holds_alternative<DeserializedVariantData>(other.m_variantData))
      {
        DeserializedVariantData const& data = std::get<DeserializedVariantData>(other.m_variantData);
        m_variantData.emplace<DeserializedVariantData>(data.signature, data.data);
      }
    }

    Variant(Variant&& other) noexcept
      : m_variantData(std::move(other.m_variantData))
    {
    }
    Variant& operator=(Variant&& other) noexcept
    {
      m_variantData = std::move(other.m_variantData);
      return *this;
    }

    Variant& operator=(Variant const& other)
    {
      if (std::holds_alternative<VariantData>(other.m_variantData))
      {
        VariantData const& data = std::get<VariantData>(other.m_variantData);
        m_variantData.emplace<VariantData>(data.signature, data.dataAlignment, data.data, data.vTable);
      }
      else if (std::holds_alternative<DeserializedVariantData>(other.m_variantData))
      {
        DeserializedVariantData const& data = std::get<DeserializedVariantData>(other.m_variantData);
        m_variantData.emplace<DeserializedVariantData>(data.signature, data.data);
      }

      return *this;
    }

    Signature const& GetSignature() const
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

      if (std::holds_alternative<std::shared_ptr<void>>(data.data))
      {
        data.vTable->marshalDataFunc(std::get<std::shared_ptr<void>>(data.data).get(), dbusType);
      }
      else
      {
        data.vTable->marshalDataFunc(std::get<std::array<byte, SMALL_BUFFER_SIZE>>(data.data).data(), dbusType);
      }
    }

    template <IsDBusType T>
    T UnmarshalData() const
    {
      if (!std::holds_alternative<DeserializedVariantData>(m_variantData))
      {
        throw VariantUnmarshalError{"Cannot unmarshal a non-deserialized variant"};
      }

      DeserializedVariantData const& data = std::get<DeserializedVariantData>(m_variantData);

      if (std::string{GetTypeSignature<T>()} != data.signature)
      {
        throw VariantUnmarshalError{
            std::format("Type signature mismatch when unmarshalling variant. Variant contains {} but we're trying to "
                        "deserialize {}",
                        data.signature.GetSignature(), std::string{GetTypeSignature<T>()})};
      }

      uint32_t arrPointer{};

      std::span<byte const> bytes;
      if (std::holds_alternative<std::shared_ptr<std::vector<byte>>>(data.data))
      {
        bytes = *std::get<std::shared_ptr<std::vector<byte>>>(data.data);
      }
      else
      {
        bytes = std::get<std::array<byte, SMALL_BUFFER_SIZE>>(data.data);
      }
      return UnmarshalDBusTypeImpl<T>(bytes, arrPointer);
    }

    bool operator==(Variant const& other) const noexcept
    {
      return m_variantData == other.m_variantData;
    }
  };

  class FastVariant
  {
   private:
    struct DeserializedVariantData
    {
      Signature signature;
      std::span<byte const> data;
    };

    std::variant<DeserializedVariantData, std::monostate> m_variantData;

   public:
    FastVariant()
      : m_variantData{std::monostate{}}
    {
    }

    FastVariant(DeserializedVariantTag, Signature&& signature, std::span<byte const>&& data)
      : m_variantData{std::monostate{}}
    {
      m_variantData.emplace<DeserializedVariantData>(std::move(signature), std::move(data));
    }

    FastVariant(FastVariant const& other)
      : m_variantData(std::monostate{})
    {
      if (std::holds_alternative<DeserializedVariantData>(other.m_variantData))
      {
        DeserializedVariantData const& data = std::get<DeserializedVariantData>(other.m_variantData);
        m_variantData.emplace<DeserializedVariantData>(data.signature, data.data);
      }
    }

    FastVariant(FastVariant&& other) noexcept
      : m_variantData(std::move(other.m_variantData))
    {
    }
    FastVariant& operator=(FastVariant&& other) noexcept
    {
      m_variantData = std::move(other.m_variantData);
      return *this;
    }

    FastVariant& operator=(FastVariant const& other)
    {
      if (std::holds_alternative<DeserializedVariantData>(other.m_variantData))
      {
        DeserializedVariantData const& data = std::get<DeserializedVariantData>(other.m_variantData);
        m_variantData.emplace<DeserializedVariantData>(data.signature, data.data);
      }

      return *this;
    }

    Signature const& GetSignature() const
    {
      if (std::holds_alternative<DeserializedVariantData>(m_variantData))
      {
        return std::get<DeserializedVariantData>(m_variantData).signature;
      }
      else
      {
        throw std::runtime_error{"Variant is in an invalid state"};
      }
    }
    uint8_t GetDataAlignment() const
    {
      if (std::holds_alternative<DeserializedVariantData>(m_variantData))
      {
        return 1;  // Deserialized data alignment is 1
      }
      else
      {
        throw std::runtime_error{"Variant is in an invalid state"};
      }
    }

    template <IsDBusType T>
    T UnmarshalData() const
    {
      if (!std::holds_alternative<DeserializedVariantData>(m_variantData))
      {
        throw VariantUnmarshalError{"Cannot unmarshal a non-deserialized variant"};
      }

      DeserializedVariantData const& data = std::get<DeserializedVariantData>(m_variantData);

      if (std::string{GetTypeSignature<T>()} != data.signature)
      {
        throw VariantUnmarshalError{
            std::format("Type signature mismatch when unmarshalling variant. Variant contains {} but we're trying to "
                        "deserialize {}",
                        data.signature.GetSignature(), std::string{GetTypeSignature<T>()})};
      }

      uint32_t arrPointer{};
      return UnmarshalDBusTypeImpl<T>(data.data, arrPointer);
    }
  };

  inline uint32_t GetSizeOfDBusTypeBasedOnSignature(std::string const& signature, std::span<byte const> dbusType,
                                                    uint32_t& arrPointer)
  {
    switch (static_cast<DBusTypeCodes>(signature[0]))
    {
      case DBusTypeCodes::BYTE:
        return sizeof(uint8_t);
      case DBusTypeCodes::BOOLEAN:
        return sizeof(bool);  // [TODO]: Investigate, should this not be uint32_t?
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
        arrPointer -=
            sizeof(uint32_t);  // Move back the pointer so we can simply skip over it in the main Unmarshal function
        return sizeof(uint32_t) + length + 1;
      }
      case DBusTypeCodes::SIGNATURE:
      {
        // Length of string as u8 + actual length of string + '\0'
        uint8_t const length = UnmarshalDBusTypeImpl<uint8_t>(dbusType, arrPointer);
        arrPointer -=
            sizeof(uint8_t);  // Move back the pointer so we can simply skip over it in the main Unmarshal function
        return sizeof(uint8_t) + length + 1;
      }
      case DBusTypeCodes::ARRAY:
      {
        // Length of array data in bytes
        uint32_t length = UnmarshalDBusTypeImpl<uint32_t>(dbusType, arrPointer);

        if (static_cast<DBusTypeCodes>(signature.at(1)) == DBusTypeCodes::DICT_BEGIN)
        {
          // Dictionary, we pad to 8-byte boundary, so we need to add padding to the size of the array data
          AddPaddingToSize(length, 8);
        }
        else
        {
          AddPaddingToSize(length, GetAlignmentOfSignature(signature[1]));
        }

        arrPointer -=
            sizeof(uint32_t);  // Move back the pointer so we can simply skip over it in the main Unmarshal function
        return sizeof(uint32_t) + length;
      }
      case DBusTypeCodes::STRUCT_BEGIN:
      {
        uint32_t length = 0;
        for (size_t i = 1; i < signature.size() - 1; ++i)
        {
          // [TODO]: This does not keep nested structs, arrays, maps, ... into account and is VERY fragile
          length += GetSizeOfDBusTypeBasedOnSignature(std::string{signature[i]}, dbusType, arrPointer);
          if (i < signature.size() - 2)
          {
            AddPaddingToSize(length, GetAlignmentOfSignature(signature[i + 1]));
          }
        }
        return length;
      }
      case DBusTypeCodes::VARIANT:
      {
        // Variant = Signature + Padding + Size of data
        Signature variantSignature = UnmarshalDBusTypeImpl<Signature>(dbusType, arrPointer);
        arrPointer -= sizeof(uint8_t) + variantSignature.size() +
                      1;  // Move back the pointer so we can simply skip over it in the main Unmarshal function

        uint32_t length = GetSizeOfDBusTypeBasedOnSignature(variantSignature.GetSignature(), dbusType, arrPointer);
        AddPaddingToSize(length, variantSignature.GetAlignmentOfSignature());
        return sizeof(uint8_t) + variantSignature.size() + 1 + length;
      }
      default:
        throw std::runtime_error{"Unsupported type for size calculation based on signature"};
    }
  }

  constexpr HeaderField HEADER_FIELDS[] = {
      HeaderField{.decimalCode = HeaderFieldCode::INVALID,
                  .type = DBusTypeCodes::INVALID,
                  .requiredMessageType = {DBusMessageType::NONE, DBusMessageType::NONE}},
      HeaderField{.decimalCode = HeaderFieldCode::PATH,
                  .type = DBusTypeCodes::OBJECT_PATH,
                  .requiredMessageType = {DBusMessageType::METHOD_CALL, DBusMessageType::SIGNAL}},
      HeaderField{.decimalCode = HeaderFieldCode::INTERFACE,
                  .type = DBusTypeCodes::STRING,
                  .requiredMessageType = {DBusMessageType::SIGNAL, DBusMessageType::NONE}},
      HeaderField{.decimalCode = HeaderFieldCode::MEMBER,
                  .type = DBusTypeCodes::STRING,
                  .requiredMessageType = {DBusMessageType::METHOD_CALL, DBusMessageType::SIGNAL}},
      HeaderField{.decimalCode = HeaderFieldCode::ERROR_NAME,
                  .type = DBusTypeCodes::STRING,
                  .requiredMessageType = {DBusMessageType::ERROR, DBusMessageType::NONE}},
      HeaderField{.decimalCode = HeaderFieldCode::REPLY_SERIAL,
                  .type = DBusTypeCodes::UINT32,
                  .requiredMessageType = {DBusMessageType::ERROR, DBusMessageType::METHOD_RETURN}},
      HeaderField{.decimalCode = HeaderFieldCode::DESTINATION,
                  .type = DBusTypeCodes::STRING,
                  .requiredMessageType = {DBusMessageType::OPTIONAL, DBusMessageType::OPTIONAL}},
      HeaderField{.decimalCode = HeaderFieldCode::SENDER,
                  .type = DBusTypeCodes::STRING,
                  .requiredMessageType = {DBusMessageType::OPTIONAL, DBusMessageType::OPTIONAL}},
      HeaderField{.decimalCode = HeaderFieldCode::SIGNATURE,
                  .type = DBusTypeCodes::SIGNATURE,
                  .requiredMessageType = {DBusMessageType::OPTIONAL, DBusMessageType::OPTIONAL}},
      HeaderField{.decimalCode = HeaderFieldCode::UNIX_FDS,
                  .type = DBusTypeCodes::UINT32,
                  .requiredMessageType = {DBusMessageType::OPTIONAL, DBusMessageType::OPTIONAL}},
  };

  class DBusSerializationError : public std::runtime_error
  {
   public:
    using std::runtime_error::runtime_error;
  };

  template <IsDBusBasicFixedType T>
  void MarshalBasicFixedType(T const& value, std::vector<byte>& dbusType)
  {
    // Fixed Type
    if constexpr (std::is_same_v<T, bool>)
    {
      // Booleans are marshalled as uint32_t
      uint32_t boolConvertedVal{static_cast<uint32_t>(value ? 1 : 0)};
      dbusType.resize(dbusType.size() + sizeof(uint32_t), 0);
      std::memcpy(dbusType.data() + dbusType.size() - sizeof(uint32_t), &boolConvertedVal, sizeof(uint32_t));
    }
    else
    {
      dbusType.resize(dbusType.size() + sizeof(T), 0);
      std::memcpy(dbusType.data() + dbusType.size() - sizeof(T), &value, sizeof(T));
    }
  }

  template <IsDBusBasicStringlikeType T>
  void MarshalBasicStringlikeType(T const& value, std::vector<byte>& dbusType)
  {
    std::string const str{value};

    if (str.contains('\0')) [[unlikely]]
    {
      throw DBusSerializationError{"Strings sent over DBus cannot contain null terminator characters"};
    }

    size_t const size{str.size()};

    if constexpr (IsString<T> || std::is_same_v<T, ObjectPath> || std::is_same_v<T, DBusInterfaceName>)
    {
      // Encode the length as a uint32_t
      MarshalBasicFixedType(static_cast<uint32_t>(size), dbusType);
    }
    else  // Signature
    {
      // Encode the length as a uint8_t
      MarshalBasicFixedType(static_cast<uint8_t>(size), dbusType);
    }

    size_t const oldSize{dbusType.size()};
    dbusType.resize(dbusType.size() + size + 1, 0);
    std::memcpy(dbusType.data() + oldSize, str.data(), size);
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

    std::vector<byte> tempBuffer;
    uint8_t const alignment = GetAlignmentOfDBusType<ContainedType>();

    for (auto const& [index, elem] : std::views::enumerate(value))
    {
      MarshalDBusTypeImpl(elem, tempBuffer);
      if (index < static_cast<int32_t>(value.size()) - 1)
      {
        // Not last element, so add padding if required
        ApplyPadding(tempBuffer, alignment);
      }
    }

    if (tempBuffer.size() >= 2 << 26)
    {
      throw std::length_error{"DBus Arrays cannot exceed a size of 64 MiB"};
    }

    // First we marshal a uint32_t fiving the length of the array (in bytes), followed by padding to the array's element
    // type boundary
    MarshalBasicFixedType(static_cast<uint32_t>(tempBuffer.size()), dbusType);
    ApplyPadding(dbusType, alignment);
#if __cpp_lib_containers_ranges
    dbusType.append_range(std::move(tempBuffer));
#else
    dbusType.insert(dbusType.end(), tempBuffer.begin(), tempBuffer.end());
#endif
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

    std::vector<byte> tempBuffer;

    for (auto it{value.cbegin()}; it != value.cend(); ++it)
    {
      if (it != value.cbegin())
      {
        ApplyPadding(tempBuffer, alignment);
      }

      MarshalDBusTypeImpl(it->first, tempBuffer);

      if constexpr (IsDBusMap<typename T::mapped_type>)
      {
        // uint32_t because maps are arrays so pad to the array.
        ApplyPadding(tempBuffer, GetAlignmentOfDBusType<uint32_t>());
      }
      else
      {
        ApplyPadding(tempBuffer, GetAlignmentOfDBusType<typename T::mapped_type>());
      }

      MarshalDBusTypeImpl(it->second, tempBuffer);
    }

    // First we marshal a uint32_t fiving the length of the array (in bytes), followed by padding to the array's element
    // type boundary
    MarshalBasicFixedType(static_cast<uint32_t>(tempBuffer.size()), dbusType);

    ApplyPadding(dbusType, alignment);

#if __cpp_lib_containers_ranges
    dbusType.append_range(std::move(tempBuffer));
#else
    dbusType.insert(dbusType.end(), tempBuffer.begin(), tempBuffer.end());
#endif

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
    else if constexpr (IsDBusVariant<T> || IsDBusFastVariant<T>)
    {
      MarshalDBusVariant(value, dbusType);
    }
    else
    {
      Logger logger{.logLevel = LogLevel::FATAL};
      LOG_FATAL(logger, "Trying to marshal type '{}' which is not a known DBus container type", ConstexprTypeName<T>());
      throw InternalError{
          std::format("Trying to marshal type '{}' which is not a known DBus container type", ConstexprTypeName<T>())};
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
      Logger logger{.logLevel = LogLevel::FATAL};
      LOG_FATAL(logger, "Trying to marshal type '{}' which is not a known DBus basic or container type",
                ConstexprTypeName<T>());
      throw InternalError{std::format("Trying to marshal type '{}' which is not a known DBus basic or container type",
                                      ConstexprTypeName<T>())};
    }
  }

  template <IsDBusType T>
  std::vector<byte> MarshalDBusType(T const& value)
  {
    std::vector<byte> dbusType{};
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
  T UnmarshalDBusBasicFixedType(std::span<byte const> dbusType, uint32_t& arrPointer)
  {
    constexpr uint32_t minSize{std::is_same_v<T, bool> ? sizeof(uint32_t) : sizeof(T)};

    if (minSize > (dbusType.size() - arrPointer)) [[unlikely]]
    {
      throw DBusMalformedInputError{
          std::format("Trying to deserialize {} but the incoming buffer (total size: {}) has only {} bytes remaining "
                      "while we need {} bytes",
                      ConstexprTypeName<T>(), dbusType.size(), dbusType.size() - arrPointer, minSize)};
    }

    T value{};
    if constexpr (std::is_same_v<T, bool>)
    {
      uint32_t boolValue{};
      std::memcpy(&boolValue, dbusType.data() + arrPointer, sizeof(uint32_t));
      value = boolValue == 1;

      arrPointer += sizeof(uint32_t);
    }
    else
    {
      std::memcpy(&value, dbusType.data() + arrPointer, sizeof(T));

      arrPointer += sizeof(T);
    }

    return value;
  }

  template <IsDBusBasicStringlikeType T>
    requires(!std::is_same_v<T, std::string_view>)
  T UnmarshalDBusBasicStringlikeType(std::span<byte const> dbusType, uint32_t& arrPointer)
  {
    uint32_t strLength{};
    if constexpr (IsString<T> || std::is_same_v<T, ObjectPath> || std::is_same_v<T, DBusInterfaceName>)
    {
      strLength = UnmarshalDBusBasicFixedType<uint32_t>(dbusType, arrPointer);
    }
    else
    {
      // Signature needs to unmarshal a u8
      strLength = static_cast<uint32_t>(UnmarshalDBusBasicFixedType<uint8_t>(dbusType, arrPointer));
    }

    if (strLength >= (dbusType.size() - arrPointer))
    {
      throw DBusMalformedInputError{
          std::format("Trying to deserialize {} with a claimed {} byte length, but the incoming buffer (total size: "
                      "{}) has only {} bytes remaining",
                      ConstexprTypeName<T>(), strLength, dbusType.size(), dbusType.size() - arrPointer)};
    }

    std::string str{reinterpret_cast<char const*>(dbusType.data()) + arrPointer, strLength};

    // + 1 to also skip the null terminator
    arrPointer += strLength + 1;

    if constexpr (IsString<T>)
    {
      return str;
    }
    else
    {
      return T{std::move(str)};
    }
  }

  template <IsDBusMultipleCompleteTypes T, size_t I, size_t MaxI>
  auto UnmarshalDBusBasicMultipleCompleteTypes(std::span<byte const> dbusType, uint32_t& arrPointer)
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
  T UnmarshalDBusBasicMultipleCompleteTypes(std::span<byte const> dbusType, uint32_t& arrPointer)
  {
    return [&dbusType, &arrPointer]<size_t... Is>(std::index_sequence<Is...>) -> T
    {
      return T{
          UnmarshalDBusBasicMultipleCompleteTypes<T, Is, std::tuple_size_v<typename T::type>>(dbusType, arrPointer)...};
    }(std::make_index_sequence<std::tuple_size_v<typename T::type>>{});
  }

  template <IsDBusBasicType T>
  T UnmarshalDBusBasicType(std::span<byte const> dbusType, uint32_t& arrPointer)
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
  T UnmarshalDBusArray(std::span<byte const> dbusType, uint32_t& arrPointer)
  {
    uint32_t const arrLength{UnmarshalDBusBasicFixedType<uint32_t>(dbusType, arrPointer)};

    if (arrLength >= 2 << 26)
    {
      throw std::length_error{"DBus Arrays cannot exceed a size of 64 MiB"};
    }

    // Remove any potential padding
    SkipPadding(arrPointer, GetAlignmentOfDBusType<typename T::value_type>());

    T vec{};

    // It's pretty hard to calculate exactly how many elements are in the array, so just reserve some arbitrary amount
    // of space [TODO]: Improve this
    if constexpr (!detail::IsArray<T>::value)
    {
      vec.reserve(8);
    }

    uint32_t index{};

    // Since the only thing we know for strings is how long the entire array is, we just keep unmarshalling strings
    // until we reach the end of the array
    uint32_t bytesRead{};
    while (bytesRead < arrLength)
    {
      if (arrPointer >= dbusType.size())
      {
        throw DBusMalformedInputError{
            std::format("Trying to deserialize {} with a claimed {} byte length, but the incoming buffer (total size: "
                        "{}) has only {} bytes remaining",
                        ConstexprTypeName<T>(), arrLength, dbusType.size(), dbusType.size() - arrPointer)};
      }

      uint32_t oldPointer{arrPointer};
      if constexpr (detail::IsArray<T>::value)
      {
        // First check if the user was correct about the fixed array's length
        if (index >= vec.size())
        {
          throw DBusDeserializationError{
              std::format("Trying to deserialize array with fixed length '{}' but received array has '{}' bytes too "
                          "many. Is your array the correct length?",
                          vec.size(), arrLength - bytesRead)};
        }

        vec[index++] = UnmarshalDBusTypeImpl<typename T::value_type>(dbusType, arrPointer);
      }
      else
      {
        vec.push_back(UnmarshalDBusTypeImpl<typename T::value_type>(dbusType, arrPointer));
      }
      bytesRead += (arrPointer - oldPointer);

      if (bytesRead < arrLength)
      {
        oldPointer = arrPointer;
        SkipPadding(arrPointer, GetAlignmentOfDBusType<typename T::value_type>());
        bytesRead += arrPointer - oldPointer;  // Remove any potential padding
      }
    }

    if constexpr (detail::IsArray<T>::value)
    {
      if (index < vec.size())
      {
        throw DBusDeserializationError{
            std::format("Fully deserialized received array with '{}' elements, but the provided fixed array expects "
                        "'{}' elements. Is your array the correct length?",
                        index, vec.size())};
      }
    }

    return vec;
  }

  template <IsDBusStruct T, size_t I, size_t MaxI>
  auto UnmarshalDBusStruct(std::span<byte const> dbusType, uint32_t& arrPointer)
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
  T UnmarshalDBusStruct(std::span<byte const> dbusType, uint32_t& arrPointer)
  {
    return [&dbusType, &arrPointer]<size_t... Is>(std::index_sequence<Is...>) -> T
    {
      return T{UnmarshalDBusStruct<T, Is, std::tuple_size_v<T>>(dbusType, arrPointer)...};
    }(std::make_index_sequence<std::tuple_size_v<T>>{});
  }

  template <IsDBusMap T>
  T UnmarshalDBusMap(std::span<byte const> dbusType, uint32_t& arrPointer)
  {
    using KeyT = typename T::key_type;
    using MappedT = typename T::mapped_type;

    uint32_t const mapLength{UnmarshalDBusTypeImpl<uint32_t>(dbusType, arrPointer)};

    uint8_t const mapAlignment{GetAlignmentOfDBusType<T>()};
    uint8_t valueAlignment{};
    if constexpr (IsDBusMap<MappedT>)
    {
      // uint32_t because maps are arrays so alignment is of the array.
      valueAlignment = GetAlignmentOfDBusType<uint32_t>();
    }
    else
    {
      valueAlignment = GetAlignmentOfDBusType<MappedT>();
    }

    uint32_t oldArrPointer{arrPointer};
    // Remove the padding between the leading uint32_t and our DICT_ENTRY
    SkipPadding(arrPointer, mapAlignment);

    T map{};

    // It's hard to tell how many elements are in a map because of the padding requirements, so just read our map until
    // we've read its full length
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
        KeyT key{UnmarshalDBusTypeImpl<KeyT>(dbusType, arrPointer)};

        SkipPadding(arrPointer, valueAlignment);

        MappedT value{UnmarshalDBusTypeImpl<MappedT>(dbusType, arrPointer)};

        map.emplace(std::move(key), std::move(value));

        // Update how much we read
        bytesRead += arrPointer - oldArrPointer;

        if (bytesRead < mapLength)
        {
          oldArrPointer = arrPointer;

          // DICT_ENTRY is a struct, so just take our T's alignment (which is guaranteed to be 8)
          AddPaddingToSize(arrPointer, mapAlignment);

          // Update how much we read
          bytesRead += arrPointer - oldArrPointer;
        }
      }
    }

    return map;
  }

  template <IsDBusVariant T>
  T UnmarshalDBusVariant(std::span<byte const> dbusType, uint32_t& arrPointer)
  {
    Signature signature{UnmarshalDBusTypeImpl<Signature>(dbusType, arrPointer)};
    SkipPadding(arrPointer, signature.GetAlignmentOfSignature());

    size_t const size = GetSizeOfDBusTypeBasedOnSignature(signature.GetSignature(), dbusType, arrPointer);
    T variant;

    if constexpr (IsDBusFastVariant<T>)
    {
      variant = T{deserialized_variant_tag, std::move(signature),
                  std::span<byte const>{dbusType.begin() + arrPointer, dbusType.begin() + arrPointer + size}};
    }
    else
    {
      if (size > Variant::SMALL_BUFFER_SIZE)
      {
#if __cpp_lib_ranges_to_container
        variant = T{
            deserialized_variant_tag, std::move(signature),
            std::move(std::ranges::to<std::vector>(dbusType | std::views::drop(arrPointer) | std::views::take(size)))};
#else
        variant = T{deserialized_variant_tag, std::move(signature),
                    std::vector<byte>(dbusType.begin() + arrPointer, dbusType.begin() + arrPointer + size)};
#endif
      }
      else
      {
        std::array<byte, Variant::SMALL_BUFFER_SIZE> arr;
        std::memcpy(arr.data(), dbusType.data() + arrPointer, size);
        variant = T{deserialized_variant_tag, std::move(signature), std::move(arr)};
      }
    }

    arrPointer += size;

    return variant;
  }

  template <IsDBusContainer T>
  T UnmarshalDBusContainer(std::span<byte const> dbusType, uint32_t& arrPointer)
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
  T UnmarshalDBusTypeImpl(std::span<byte const> dbusType, uint32_t& arrPointer)
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
  T UnmarshalDBusType(std::span<byte const> dbusType, std::string const& signature, uint32_t& arrPointer)
  {
    if (!IsDBusTypeCode(signature))
    {
      throw DBusInvalidSignatureError{std::format("Signature '{}' contains unknown DBus Type Codes.", signature)};
    }

    // [TODO]: stop being lazy and check this at compile time
    if (!AreDBusTypeCodeBracketsEven(signature))
    {
      throw DBusInvalidSignatureError{
          std::format("Signature '{}' is incorrect and contains an uneven amount of brackets", signature)};
    }

    if (signature != GetTypeSignature<T>())
    {
      throw DBusInvalidSignatureError{std::format("Type {} (signature: '{}') and Signature {} do not match.",
                                                  ConstexprTypeName<T>(), std::string{GetTypeSignature<T>()},
                                                  signature)};
    }

    return UnmarshalDBusTypeImpl<T>(dbusType, arrPointer);
  }

  template <IsDBusType T>
    requires(!IsRawStringLiteral<std::decay_t<T>>)
  T UnmarshalDBusType(std::vector<byte> const& dbusType, std::string const& signature)
  {
    uint32_t arrPointer{};
    T value{UnmarshalDBusType<T>(dbusType, signature, arrPointer)};

    if (arrPointer != dbusType.size()) [[unlikely]]
    {
      throw DBusMalformedInputError{
          std::format("Deserialized {} but the incoming buffer (total size: {}) has {} bytes remaining",
                      ConstexprTypeName<T>(), dbusType.size(), dbusType.size() - arrPointer)};
    }

    return value;
  }
  template <IsDBusType T>

    requires(!IsRawStringLiteral<std::decay_t<T>>)
  T UnmarshalDBusType(std::span<byte const> dbusType, std::string const& signature)
  {
    uint32_t arrPointer{};
    T value{UnmarshalDBusType<T>(dbusType, signature, arrPointer)};

    if (arrPointer != dbusType.size()) [[unlikely]]
    {
      throw DBusMalformedInputError{
          std::format("Deserialized {} but the incoming buffer (total size: {}) has {} bytes remaining",
                      ConstexprTypeName<T>(), dbusType.size(), dbusType.size() - arrPointer)};
    }

    return value;
  }

  extern template class MultipleCompleteTypes<std::string>;
  extern template class MultipleCompleteTypes<std::string, std::string>;
  extern template class MultipleCompleteTypes<std::string, std::string, Variant>;
  extern template class MultipleCompleteTypes<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t>;

  extern template std::vector<byte> MarshalDBusType<bool>(bool const&);
  extern template std::vector<byte> MarshalDBusType<uint8_t>(uint8_t const&);
  extern template std::vector<byte> MarshalDBusType<uint16_t>(uint16_t const&);
  extern template std::vector<byte> MarshalDBusType<int16_t>(int16_t const&);
  extern template std::vector<byte> MarshalDBusType<uint32_t>(uint32_t const&);
  extern template std::vector<byte> MarshalDBusType<int32_t>(int32_t const&);
  extern template std::vector<byte> MarshalDBusType<uint64_t>(uint64_t const&);
  extern template std::vector<byte> MarshalDBusType<int64_t>(int64_t const&);
  extern template std::vector<byte> MarshalDBusType<float>(float const&);
  extern template std::vector<byte> MarshalDBusType<double>(double const&);
  extern template std::vector<byte> MarshalDBusType<std::string>(std::string const&);
  extern template std::vector<byte> MarshalDBusType<std::string_view>(std::string_view const&);
  extern template std::vector<byte> MarshalDBusType<char const*>(char const* const&);
  extern template std::vector<byte> MarshalDBusType<Variant>(Variant const&);
  extern template std::vector<byte> MarshalDBusType<Signature>(Signature const&);
  extern template std::vector<byte> MarshalDBusType<ObjectPath>(ObjectPath const&);
  extern template std::vector<byte> MarshalDBusType<DBusInterfaceName>(DBusInterfaceName const&);

  extern template std::vector<byte> MarshalDBusType<std::vector<bool>>(std::vector<bool> const&);
  extern template std::vector<byte> MarshalDBusType<std::vector<uint8_t>>(std::vector<uint8_t> const&);
  extern template std::vector<byte> MarshalDBusType<std::vector<uint16_t>>(std::vector<uint16_t> const&);
  extern template std::vector<byte> MarshalDBusType<std::vector<int16_t>>(std::vector<int16_t> const&);
  extern template std::vector<byte> MarshalDBusType<std::vector<uint32_t>>(std::vector<uint32_t> const&);
  extern template std::vector<byte> MarshalDBusType<std::vector<int32_t>>(std::vector<int32_t> const&);
  extern template std::vector<byte> MarshalDBusType<std::vector<uint64_t>>(std::vector<uint64_t> const&);
  extern template std::vector<byte> MarshalDBusType<std::vector<int64_t>>(std::vector<int64_t> const&);
  extern template std::vector<byte> MarshalDBusType<std::vector<float>>(std::vector<float> const&);
  extern template std::vector<byte> MarshalDBusType<std::vector<double>>(std::vector<double> const&);
  extern template std::vector<byte> MarshalDBusType<std::vector<std::string>>(std::vector<std::string> const&);
  extern template std::vector<byte> MarshalDBusType<std::vector<std::string_view>>(
      std::vector<std::string_view> const&);
  extern template std::vector<byte> MarshalDBusType<std::vector<char const*>>(std::vector<char const*> const&);
  extern template std::vector<byte> MarshalDBusType<std::vector<Variant>>(std::vector<Variant> const&);
  extern template std::vector<byte> MarshalDBusType<std::vector<Signature>>(std::vector<Signature> const&);
  extern template std::vector<byte> MarshalDBusType<std::vector<ObjectPath>>(std::vector<ObjectPath> const&);
  extern template std::vector<byte> MarshalDBusType<std::vector<DBusInterfaceName>>(
      std::vector<DBusInterfaceName> const&);

  extern template std::vector<byte> MarshalDBusType<std::map<std::string, std::string>>(
      std::map<std::string, std::string> const&);
  extern template std::vector<byte> MarshalDBusType<std::map<std::string, Variant>>(
      std::map<std::string, Variant> const&);

  extern template std::vector<byte> MarshalDBusType<MultipleCompleteTypes<std::string>>(
      MultipleCompleteTypes<std::string> const&);
  extern template std::vector<byte> MarshalDBusType<MultipleCompleteTypes<std::string, std::string>>(
      MultipleCompleteTypes<std::string, std::string> const&);
  extern template std::vector<byte> MarshalDBusType<MultipleCompleteTypes<std::string, std::string, Variant>>(
      MultipleCompleteTypes<std::string, std::string, Variant> const&);
  extern template std::vector<byte>
  MarshalDBusType<MultipleCompleteTypes<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t>>(
      MultipleCompleteTypes<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t> const&);
}  // namespace cxxbus
