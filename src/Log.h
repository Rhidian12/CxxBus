#pragma once

#include <string_view>

namespace cxxbus
{
  enum class LogLevel
  {
    Trace,
    Debug,
    Info,
    Error,
    Off,  // By design: Fatal should ALWAYS be logged because something is TRULY fucked up
    Fatal,
  };

  struct Logger
  {
    LogLevel logLevel = LogLevel::Info;

    void LogTrace(std::string_view message) const;
    void LogDebug(std::string_view message) const;
    void LogInfo(std::string_view message) const;
    void LogError(std::string_view message) const;
    void LogFatal(std::string_view message) const;
  };
}  // namespace cxxbus