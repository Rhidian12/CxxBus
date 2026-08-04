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

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace cxxbus
{
  template <typename T>
  static consteval std::string_view WrappedTypeName()
  {
    return __PRETTY_FUNCTION__;  // [TODO]: Make this cross-platform friendly
  }

  /* Reference for TypeName: https://stackoverflow.com/questions/35941045/can-i-obtain-c-type-names-in-a-constexpr-way
   */
  template <typename T>
  static consteval std::string_view ConstexprTypeName()
  {
    std::string_view constexpr wrappedName(WrappedTypeName<T>());

#ifdef __clang__
    // Several examples:std::string_view WrappedTypeName() [T = std::map<int, std::vector<std::string>>]
    // std::string_view WrappedTypeName() [T = int]
    // std::string_view WrappedTypeName() [T = std::string]
    // std::string_view WrappedTypeName() [T = std::vector<int>]
    // std::string_view WrappedTypeName() [T = std::map<int, std::vector<std::string>>]
    constexpr size_t endOfType{wrappedName.find_last_of(']')};
    constexpr size_t beginOfType{wrappedName.find("[T = ") + 4};
#elif __GNUC__
    // Several examples:
    // std::string_view WrappedTypeName() [with T = int; std::string_view = std::basic_string_view<char>]
    // std::string_view WrappedTypeName() [with T = std::__cxx11::basic_string<char>; std::string_view =
    // std::basic_string_view<char>] std::string_view WrappedTypeName() [with T = std::vector<int>; std::string_view =
    // std::basic_string_view<char>] std::string_view WrappedTypeName() [with T = std::map<int,
    // std::vector<std::__cxx11::basic_string<char> > >; std::string_view = std::basic_string_view<char>]
    constexpr size_t endOfType{wrappedName.find_last_of(";")};
    constexpr size_t beginOfType{wrappedName.find("with T = ") + 8};
#else
#error MSVC not supported yet
#endif

    return wrappedName.substr(beginOfType + 1, endOfType - beginOfType - 1);
  }

#ifdef __clang__
  static_assert(ConstexprTypeName<int>() == "int");
  static_assert(ConstexprTypeName<std::string>() == "std::basic_string<char>");
  static_assert(ConstexprTypeName<std::vector<int>>() == "std::vector<int>");
  static_assert(ConstexprTypeName<std::map<int, std::vector<std::string>>>() ==
                "std::map<int, std::vector<std::basic_string<char>>>");
#elif __GNUC__
#else
  static_assert(ConstexprTypeName<int>() == "int");
  static_assert(ConstexprTypeName<std::string>() == "std::__cxx11::basic_string<char>");
  static_assert(ConstexprTypeName<std::vector<int>>() == "std::vector<int>");
  static_assert(ConstexprTypeName<std::map<int, std::vector<std::string>>>() ==
                "std::map<int, std::vector<std::__cxx11::basic_string<char> > >");
#error MSVC not supported yet
#endif
}  // namespace cxxbus
