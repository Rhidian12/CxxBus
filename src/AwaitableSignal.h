#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/signals2/signal.hpp>
#include <functional>
#include <vector>

namespace cxxbus
{
  // Combiner for boost::signals2::signal to combine returned Deferreds into one that can be awaited
  template <typename T>
  struct AwaitableCombiner
  {
    using result_type = decltype(std::declval<T&>()());

    template <typename InputIterator>
    result_type operator()(InputIterator first, InputIterator last) const
    {
      // operator() here MUST finish before we actually suspend our coroutine (therefore operator() CANNOT be a coroutine)
      // Otherwise the iterators we receive become dangling and we get crashes
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
      for (auto &a : awaitables)
      {
        co_await std::move(a);
      }

      co_return;
    }
  };

  template <typename ReturnType>
  using AwaitableCombinerFactory = std::function<boost::asio::awaitable<ReturnType>()>;
  template <typename ReturnType, typename... Args>
  using AwaitableSignal = boost::signals2::signal<AwaitableCombinerFactory<ReturnType>(Args...), AwaitableCombiner<AwaitableCombinerFactory<ReturnType>>>;

}  // namespace cxxbus