#include "Log.h"

#include <iostream>

void Logger::LogInfo(std::string_view message) const
{
  if (logLevel > LogLevel::Info) return;
  std::cout << "[INFO] " << message << std::endl;
}

void Logger::LogTrace(std::string_view message) const
{
  if (logLevel > LogLevel::Trace) return;
  std::cout << "[TRACE] " << message << std::endl;
}

void Logger::LogDebug(std::string_view message) const
{
  if (logLevel > LogLevel::Debug) return;
  std::cout << "[DEBUG] " << message << std::endl;
}

void Logger::LogError(std::string_view message) const
{
  if (logLevel > LogLevel::Error) return;
  std::cerr << "\033[31m[ERROR] " << message << "\033[m" << std::endl;
}

void Logger::LogFatal(std::string_view message) const
{
  // Always log Fatal errors
  std::cerr << "\033[31;4m[FATAL] " << message << "\033[m" << std::endl;
}