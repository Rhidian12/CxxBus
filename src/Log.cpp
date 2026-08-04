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

#include "Log.h"

#include <format>
#include <iostream>

namespace cxxbus
{
  namespace
  {
    constexpr std::string_view CYAN = "\033[0;36m";                     // TRACE
    constexpr std::string_view BLUE = "\033[0;34m";                     // DEBUG
    constexpr std::string_view GREEN = "\033[0;32m";                    // INFO
    constexpr std::string_view RED = "\033[0;31m";                      // ERROR
    constexpr std::string_view RED_BOLD_UNDERLINED = "\033[0;1;4;31m";  // FATAL
    constexpr std::string_view RESET = "\033[0m";                       // Reset colours

    std::string_view GetLogColour(LogLevel level)
    {
      switch (level)
      {
        case LogLevel::Off:
          throw std::runtime_error{"Cannot be logging OFF loglevel"};
        case LogLevel::Trace:
          return CYAN;
        case LogLevel::Debug:
          return BLUE;
        case LogLevel::Info:
          return GREEN;
        case LogLevel::Error:
          return RED;
        case LogLevel::Fatal:
          return RED_BOLD_UNDERLINED;
      }
    }
  }  // namespace

  void Logger::LogInfo(std::string_view message) const
  {
    if (logLevel > LogLevel::Info) return;
    std::cout << std::format("{}[INFO] {}{}", GetLogColour(LogLevel::Info), RESET, message) << std::endl;
  }

  void Logger::LogTrace(std::string_view message) const
  {
    if (logLevel > LogLevel::Trace) return;
    std::cout << std::format("{}[TRACE] {}{}", GetLogColour(LogLevel::Trace), RESET, message) << std::endl;
  }

  void Logger::LogDebug(std::string_view message) const
  {
    if (logLevel > LogLevel::Debug) return;
    std::cout << std::format("{}[DEBUG] {}{}", GetLogColour(LogLevel::Debug), RESET, message) << std::endl;
  }

  void Logger::LogError(std::string_view message) const
  {
    if (logLevel > LogLevel::Error) return;
    std::cout << std::format("{}[ERROR] {}{}", GetLogColour(LogLevel::Error), RESET, message) << std::endl;
  }

  void Logger::LogFatal(std::string_view message) const
  {
    // Always log Fatal errors
    std::cout << std::format("{}[FATAL] {}{}", GetLogColour(LogLevel::Fatal), RESET, message) << std::endl;
  }
}  // namespace cxxbus
