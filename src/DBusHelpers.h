#pragma once

#include "DBusConcepts.h"

class Signature;
class ObjectPath;
class Variant;
template <typename... Ts>
class MultipleCompleteTypes;

bool IsDBusBasicFixedTypeCode(unsigned char c);
bool IsDBusBasicStringlikeTypeCode(unsigned char c);
bool IsDBusBasicTypeCode(unsigned char c);
bool IsDBusContainerTypeCode(unsigned char c);
bool IsDBusTypeCode(unsigned char c);
bool IsDBusTypeCode(std::string const & str);
bool AreDBusTypeCodeBracketsEven(std::string const& str);
uint8_t GetAlignmentOfSignature(Signature const& signature);

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
  else if constexpr (IsDBusVariant<T>)
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

template <IsDBusType T>
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
      return 4;
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
