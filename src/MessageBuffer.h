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

#include <array>
#include <cassert>
#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>

#include "DBusTypes.h"

namespace cxxbus
{
  // Custom deleter for pointers gotten from our `LargeBufferPool`
  // Instead of actually freeing memory we simply mark the buffer as free to be used
  struct MessageBufferDeleter
  {
    struct NotPartOfPoolTagType
    {
    };
    inline constexpr static NotPartOfPoolTagType NotPartOfPoolTag;

    uint32_t capacity{};
    size_t index{};

    MessageBufferDeleter() = default;  // required for default construction
    explicit MessageBufferDeleter(size_t bufferCapacity, size_t bufferIndex)
      : capacity(bufferCapacity)
      , index(bufferIndex)
    {
    }
    explicit MessageBufferDeleter(NotPartOfPoolTagType)
      : capacity(std::numeric_limits<uint32_t>::max())
      , index(std::numeric_limits<size_t>::max())
    {
    }

    void operator()(byte* data) const noexcept;
  };

  using MessageBuffer = std::unique_ptr<byte[], MessageBufferDeleter>;

  namespace detail
  {
    // Class that keeps large allocations pooled together so that we don't constantly need to `malloc()` or `free()`
    // buffers of 128KB or more.
    // The minimum buffer size can be set via CMake configuration options.
    // The tradeoff is the following:
    // the smaller the buffer size, the more memory the program will use as this LargeBufferPool will not release any of
    // its buffers, but the faster the program will run with DBus messages of a significant size Only change this
    // default if you know what you're doing and after benchmarking the difference.
    //
    // The maximum buffer size can also be set via CMake configuration options.
    // The tradeoff is the same as for the minimum buffer size: the greater the size, the more memory will be used for
    // the duration of the program, but large messages will be handled more efficiently. Only change this default if you
    // know what you're doing and after benchmarking the difference.
    //
    // The maximum amount of buffers is also configurable via CMake. How many buffers you need / want highly depends on
    // your needs and there is no 1 good answer.
    class LargeBufferPool
    {
     private:
      inline static constexpr size_t MIN_POOLED_SIZE{CXXBUS_MIN_LARGE_BUFFER_SIZE * 1024};         // 128KiB by default
      inline static constexpr size_t MAX_POOLED_SIZE{CXXBUS_MAX_LARGE_BUFFER_SIZE * 1024 * 1024};  // 20MiB by default
      inline static constexpr size_t MAX_BUFFERS{
          CXXBUS_NR_OF_LARGE_BUFFERS};  // Max buffers we can maintain at once. 8 by default
      inline static constexpr size_t GRANULARITY{
          64 * 1024};  // 64 KiB. Round up big allocations to this so that they try to re-use buffers

     private:
      struct Entry
      {
        MessageBuffer data{};
        size_t capacity{};
        bool free{true};
      };

     private:
      std::mutex m_mutex;
      std::array<Entry, MAX_BUFFERS> m_entries{};
      bool m_destroying{};

     private:
      LargeBufferPool() = default;
      ~LargeBufferPool();

      // Get the size for our allocation. We're assuming here that `minimumSize >= MIN_POOLED_SIZE && minimumSize <
      // MAX_POOLED_SIZE`
      static constexpr size_t GetGranularizedSize(size_t minimumSize)
      {
        return minimumSize + (GRANULARITY - (minimumSize % GRANULARITY));
      }

     public:
      static LargeBufferPool& Instance()
      {
        static LargeBufferPool instance{};
        return instance;
      }

      static constexpr bool IsLargeEnoughAllocation(size_t capacity)
      {
        return capacity >= MIN_POOLED_SIZE && capacity <= MAX_POOLED_SIZE;
      }

      // Returns a pointer to a valid buffer if `minimumSize` meets the buffer size requirements
      // Returns a nullptr if `minimumSize` does not meet the buffer size requirements
      MessageBuffer Acquire(size_t minimumSize)
      {
        if (IsLargeEnoughAllocation(minimumSize))
        {
          size_t actualSize{GetGranularizedSize(minimumSize)};
          std::unique_lock<std::mutex> lock{m_mutex};
          for (size_t i{}; i < MAX_BUFFERS; ++i)
          {
            Entry& entry = m_entries[i];
            // Check if there's a buffer that is currently not being used
            if (entry.free && (entry.capacity >= actualSize || entry.capacity == 0))
            {
              entry.free = false;
              if (!entry.data)
              {
                // If we haven't allocated a buffer yet, allocate one
                entry.data = MessageBuffer{new byte[actualSize]{}, MessageBufferDeleter{actualSize, i}};
                entry.capacity = actualSize;
              }

              return std::move(entry.data);
            }
          }
        }

        return nullptr;
      }

      void Release(byte* data, size_t index) noexcept
      {
        if (m_destroying)
        {
          delete[] data;
          return;
        }

        std::unique_lock<std::mutex> lock{m_mutex};
        m_entries[index].free = true;
        assert(!m_entries[index].data);
        m_entries[index].data = MessageBuffer{data, MessageBufferDeleter{m_entries[index].capacity, index}};
      }
    };
  }  // namespace detail

  // Returns either a buffer from the `LargeBufferPool` or allocates the requested size exactly
  MessageBuffer AllocateMessageBuffer(size_t minimumSize, uint32_t& capacity);
}  // namespace cxxbus
