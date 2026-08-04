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
#include <boost/signals2/signal.hpp>
#include <functional>
#include <vector>

namespace cxxbus
{
  // Combiner for boost::signals2::signal to combine returned awaitables into one that can be awaited
  template <typename T>
  struct AwaitableCombiner
  {
    using result_type = decltype(std::declval<T&>()());

    template <typename InputIterator>
    result_type operator()(InputIterator first, InputIterator last) const
    {
      // operator() here MUST finish before we actually suspend our coroutine (therefore operator() CANNOT be a
      // coroutine) Otherwise the iterators we receive become dangling and we get crashes
      std::vector<result_type> awaitables;
      for (auto it = first; it != last; ++it)
      {
        awaitables.emplace_back((*it)());
      }

      return awaitAll(std::move(awaitables));
    }

   private:
    static result_type awaitAll(std::vector<result_type> awaitables)
    {
      for (auto& a : awaitables)
      {
        co_await std::move(a);
      }

      co_return;
    }
  };

  template <typename ReturnType>
  using AwaitableCombinerFactory = std::function<boost::asio::awaitable<ReturnType>()>;
  template <typename ReturnType, typename... Args>
  using AwaitableSignal = boost::signals2::signal<AwaitableCombinerFactory<ReturnType>(Args...),
                                                  AwaitableCombiner<AwaitableCombinerFactory<ReturnType>>>;

}  // namespace cxxbus
