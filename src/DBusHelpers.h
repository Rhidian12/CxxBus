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

#include <boost/asio/awaitable.hpp>
#include <tuple>

#include "DBusConcepts.h"
#include "DBusTypes.h"

namespace cxxbus
{
  class Signature;
  class ObjectPath;
  class Variant;
  template <typename... Ts>
  class MultipleCompleteTypes;
  class IncomingDBusMessage;

  bool IsDBusBasicFixedTypeCode(unsigned char c);
  bool IsDBusBasicStringlikeTypeCode(unsigned char c);
  bool IsDBusBasicTypeCode(unsigned char c);
  bool IsDBusContainerTypeCode(unsigned char c);
  bool IsDBusTypeCode(unsigned char c);
  bool IsDBusTypeCode(std::string const& str);
  bool AreDBusTypeCodeBracketsEven(std::string const& str);
  uint8_t GetAlignmentOfSignature(char const signature);
  std::string ParseDBusAddress(BusType busType);
  std::string HexEncodeString(std::string const& str);

  template <size_t N>
  class ConstexprString
  {
   private:
    template <size_t N2>
    friend class ConstexprString;

   private:
    std::array<char, N + 1> data;
    size_t size = N;

   public:
    constexpr ConstexprString() = default;
    constexpr ConstexprString(char const (&str)[N + 1])
    {
      for (size_t i{}; i < N; ++i)
      {
        data[i] = str[i];
      }
      data[N] = '\0';
    }

    template <size_t N2>
    constexpr ConstexprString<N + N2> Concat(ConstexprString<N2> str)
    {
      ConstexprString<N + N2> newStr{};
      for (size_t i{}; i < N; ++i)
      {
        newStr.data[i] = data[i];
      }
      for (size_t i{}; i < N2; ++i)
      {
        newStr.data[N + i] = str.data[i];
      }
      newStr.data[N + N2] = '\0';
      return newStr;
    }

    template <size_t N2>
    constexpr bool operator==(ConstexprString<N2> other) const noexcept
    {
      return N == N2 && data == other.data;
    }

    bool operator==(std::string const& other) const noexcept
    {
      if (N != other.size()) return false;

      for (size_t i{}; i < N; ++i)
      {
        if (data[i] != other[i])
        {
          return false;
        }
      }
      return true;
    }

    explicit operator std::string() const
    {
      return std::string{data.data()};
    }
  };

  template <IsDBusType T>
  constexpr auto GetTypeSignature();

  template <typename T, size_t I>
  constexpr auto BuildStructSignatureImpl()
  {
    if constexpr (I == std::tuple_size_v<T>)
    {
      return ConstexprString<1>{")"};
    }
    else
    {
      return GetTypeSignature<std::tuple_element_t<I, T>>().Concat(BuildStructSignatureImpl<T, I + 1>());
    }
  }

  template <typename T>
  constexpr auto BuildStructSignature()
  {
    return ConstexprString<1>{"("}.Concat(BuildStructSignatureImpl<T, 0>());
  }

  template <typename T, size_t I>
  constexpr auto BuildMultipleCompleteTypesSignature()
  {
    if constexpr (I == std::tuple_size_v<typename T::type>)
    {
      return ConstexprString<0>{};
    }
    else
    {
      return GetTypeSignature<std::tuple_element_t<I, typename T::type>>().Concat(
          BuildMultipleCompleteTypesSignature<T, I + 1>());
    }
  }

  template <IsDBusType T>
  constexpr auto GetTypeSignature()
  {
    if constexpr (std::is_same_v<T, uint8_t>)
    {
      return ConstexprString<1>{"y"};
    }
    else if constexpr (std::is_same_v<T, bool>)
    {
      return ConstexprString<1>{"b"};
    }
    else if constexpr (std::is_same_v<T, int16_t>)
    {
      return ConstexprString<1>{"n"};
    }
    else if constexpr (std::is_same_v<T, uint16_t>)
    {
      return ConstexprString<1>{"q"};
    }
    else if constexpr (std::is_same_v<T, int32_t>)
    {
      return ConstexprString<1>{"i"};
    }
    else if constexpr (std::is_same_v<T, uint32_t>)
    {
      return ConstexprString<1>{"u"};
    }
    else if constexpr (std::is_same_v<T, int64_t>)
    {
      return ConstexprString<1>{"x"};
    }
    else if constexpr (std::is_same_v<T, uint64_t>)
    {
      return ConstexprString<1>{"t"};
    }
    else if constexpr (std::is_same_v<T, double>)
    {
      return ConstexprString<1>{"d"};
    }
    else if constexpr (IsString<T> || std::same_as<T, DBusInterfaceName>)
    {
      return ConstexprString<1>{"s"};
    }
    else if constexpr (std::is_same_v<T, ObjectPath>)
    {
      return ConstexprString<1>{"o"};
    }
    else if constexpr (std::is_same_v<T, Signature>)
    {
      return ConstexprString<1>{"g"};
    }
    else if constexpr (IsDBusArray<T>)
    {
      return ConstexprString<1>{"a"}.Concat(GetTypeSignature<typename T::value_type>());
    }
    else if constexpr (IsDBusStruct<T>)
    {
      return BuildStructSignature<T>();
    }
    else if constexpr (IsDBusVariant<T>)
    {
      return ConstexprString<1>{"v"};
    }
    else if constexpr (IsDBusMap<T>)
    {
      return ConstexprString<2>{"a{"}
          .Concat(GetTypeSignature<typename T::key_type>())
          .Concat(GetTypeSignature<typename T::mapped_type>())
          .Concat(ConstexprString<1>{"}"});
    }
    else if constexpr (IsDBusMultipleCompleteTypes<T>)
    {
      return BuildMultipleCompleteTypesSignature<T, 0>();
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
    else if constexpr (std::is_same_v<T, uint64_t> || std::is_same_v<T, int64_t> || std::is_same_v<T, double> ||
                       std::is_same_v<T, float>)
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
}  // namespace cxxbus
