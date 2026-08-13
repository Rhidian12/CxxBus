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

#include <format>
#include <string_view>

namespace cxxbus
{
  enum class LogLevel
  {
    TRACE,
    DEBUG,
    INFO,
    ERROR,
    OFF,  // By design: Fatal should ALWAYS be logged because something is TRULY fucked up
    FATAL,
  };

  struct Logger
  {
    LogLevel logLevel = LogLevel::INFO;

    void LogImpl(LogLevel wantedLogLevel, std::string_view message) const;

    template <typename... Args>
    void LogTrace(std::string_view format, Args&&... args) const
    {
      LogImpl(LogLevel::TRACE, std::vformat(format, std::make_format_args(args...)));
    }

    template <typename... Args>
    void LogDebug(std::string_view format, Args&&... args) const
    {
      LogImpl(LogLevel::DEBUG, std::vformat(format, std::make_format_args(args...)));
    }

    template <typename... Args>
    void LogInfo(std::string_view format, Args&&... args) const
    {
      LogImpl(LogLevel::INFO, std::vformat(format, std::make_format_args(args...)));
    }

    template <typename... Args>
    void LogError(std::string_view format, Args&&... args) const
    {
      LogImpl(LogLevel::ERROR, std::vformat(format, std::make_format_args(args...)));
    }

    template <typename... Args>
    void LogFatal(std::string_view format, Args&&... args) const
    {
      LogImpl(LogLevel::FATAL, std::vformat(format, std::make_format_args(args...)));
    }
  };
}  // namespace cxxbus
