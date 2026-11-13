#include <benchmark/benchmark.h>
#include <sys/types.h>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <cstdint>

#include "StructToTuple.h"
#include "src/DBusConnection.h"
#include "src/DBusMessage.h"
#include "src/DBusTypes.h"
#include "src/IncomingDBusMessage.h"

cxxbus::DBusWellKnownName const WELL_KNOWN_NAME = cxxbus::DBusWellKnownName{"org.cxxbus.test"};
cxxbus::ObjectPath const OBJECT_PATH = cxxbus::ObjectPath{"/org/cxxbus/test"};
std::string const METHOD_NAME = "Benchmark";

static void CxxBus_EmptyMessage(benchmark::State& state)
{
  boost::asio::io_context ioContext{1};
  auto work = [&ioContext, &state]() -> boost::asio::awaitable<void>
  {
    auto serverConn = co_await cxxbus::DBusConnection::Create(ioContext, WELL_KNOWN_NAME, cxxbus::BusType::SESSION);
    auto clientConn = co_await cxxbus::DBusConnection::Create(ioContext, std::nullopt, cxxbus::BusType::SESSION);

    co_await serverConn->RegisterObjectPathHandler(
        OBJECT_PATH, [serverConn](cxxbus::IncomingDBusMessage const& msg) -> boost::asio::awaitable<void>
        { co_return co_await serverConn->SendMessageNoReply(cxxbus::DBusMessage::Reply(msg)); });

    for (auto _ : state)
    {
      co_await clientConn->SendMessage(
          cxxbus::DBusMessage::Method(METHOD_NAME, OBJECT_PATH).Destination(WELL_KNOWN_NAME.GetName()));
    }

    co_await serverConn->Close();
    co_await clientConn->Close();
  };

  boost::asio::co_spawn(ioContext, work(), boost::asio::detached);

  ioContext.run();
}

static void CxxBus_StringMessage(benchmark::State& state)
{
  boost::asio::io_context ioContext{};
  auto work = [&ioContext, &state]() -> boost::asio::awaitable<void>
  {
    auto serverConn = co_await cxxbus::DBusConnection::Create(ioContext, WELL_KNOWN_NAME, cxxbus::BusType::SESSION);
    auto clientConn = co_await cxxbus::DBusConnection::Create(ioContext, std::nullopt, cxxbus::BusType::SESSION);

    co_await serverConn->RegisterObjectPathHandler(
        OBJECT_PATH, [serverConn](cxxbus::IncomingDBusMessage const& msg) -> boost::asio::awaitable<void>
        { co_return co_await serverConn->SendMessageNoReply(cxxbus::DBusMessage::Reply(msg)); });

    std::string str{};
    for (int i{}; i < 100; ++i)
    {
      str.push_back(std::max(i % 127, 1));
    }

    for (auto _ : state)
    {
      co_await clientConn->SendMessage(
          cxxbus::DBusMessage::Method(METHOD_NAME, OBJECT_PATH).Destination(WELL_KNOWN_NAME.GetName()).Parameter(str));
    }

    co_await serverConn->Close();
    co_await clientConn->Close();
  };

  boost::asio::co_spawn(ioContext, work(), boost::asio::detached);

  ioContext.run();
}

static void CxxBus_BigStringMessage(benchmark::State& state)
{
  boost::asio::io_context ioContext{};
  auto work = [&ioContext, &state]() -> boost::asio::awaitable<void>
  {
    auto serverConn = co_await cxxbus::DBusConnection::Create(ioContext, WELL_KNOWN_NAME, cxxbus::BusType::SESSION);
    auto clientConn = co_await cxxbus::DBusConnection::Create(ioContext, std::nullopt, cxxbus::BusType::SESSION);

    co_await serverConn->RegisterObjectPathHandler(
        OBJECT_PATH, [serverConn](cxxbus::IncomingDBusMessage const& msg) -> boost::asio::awaitable<void>
        { co_return co_await serverConn->SendMessageNoReply(cxxbus::DBusMessage::Reply(msg)); });

    std::string str{};
    for (int i{}; i < 10'000; ++i)
    {
      str.push_back(std::max(i % 127, 1));
    }

    for (auto _ : state)
    {
      co_await clientConn->SendMessage(
          cxxbus::DBusMessage::Method(METHOD_NAME, OBJECT_PATH).Destination(WELL_KNOWN_NAME.GetName()).Parameter(str));
    }

    co_await serverConn->Close();
    co_await clientConn->Close();
  };

  boost::asio::co_spawn(ioContext, work(), boost::asio::detached);

  ioContext.run();
}

static void CxxBus_StructMessage(benchmark::State& state)
{
  boost::asio::io_context ioContext{};
  auto work = [&ioContext, &state]() -> boost::asio::awaitable<void>
  {
    auto serverConn = co_await cxxbus::DBusConnection::Create(ioContext, WELL_KNOWN_NAME, cxxbus::BusType::SESSION);
    auto clientConn = co_await cxxbus::DBusConnection::Create(ioContext, std::nullopt, cxxbus::BusType::SESSION);

    co_await serverConn->RegisterObjectPathHandler(
        OBJECT_PATH, [serverConn](cxxbus::IncomingDBusMessage const& msg) -> boost::asio::awaitable<void>
        { co_return co_await serverConn->SendMessageNoReply(cxxbus::DBusMessage::Reply(msg)); });

    struct Foo
    {
      std::string s = "Hello World!";
      double d = 1.123456789;
      int32_t i = 987654321;
      uint16_t u = 12345;
      bool b = true;
    };
    Foo f;
    auto fooTuple = StT::StructToTuple(f);

    for (auto _ : state)
    {
      co_await clientConn->SendMessage(cxxbus::DBusMessage::Method(METHOD_NAME, OBJECT_PATH)
                                           .Destination(WELL_KNOWN_NAME.GetName())
                                           .Parameter(fooTuple));
    }

    co_await serverConn->Close();
    co_await clientConn->Close();
  };

  boost::asio::co_spawn(ioContext, work(), boost::asio::detached);

  ioContext.run();
}

static void CxxBus_NestedStructMessage(benchmark::State& state)
{
  boost::asio::io_context ioContext{};
  auto work = [&ioContext, &state]() -> boost::asio::awaitable<void>
  {
    auto serverConn = co_await cxxbus::DBusConnection::Create(ioContext, WELL_KNOWN_NAME, cxxbus::BusType::SESSION);
    auto clientConn = co_await cxxbus::DBusConnection::Create(ioContext, std::nullopt, cxxbus::BusType::SESSION);

    co_await serverConn->RegisterObjectPathHandler(
        OBJECT_PATH, [serverConn](cxxbus::IncomingDBusMessage const& msg) -> boost::asio::awaitable<void>
        { co_return co_await serverConn->SendMessageNoReply(cxxbus::DBusMessage::Reply(msg)); });

    struct Foo
    {
      std::string s = "Hello World!";
      double d = 1.123456789;
      int32_t i = 987654321;
      uint16_t u = 12345;
      bool b = true;
    };
    struct Bar
    {
      Foo a;
      Foo b;
      Foo c;
    };
    Bar b{};
    auto fooTuple = StT::StructToTuple(b);

    for (auto _ : state)
    {
      co_await clientConn->SendMessage(cxxbus::DBusMessage::Method(METHOD_NAME, OBJECT_PATH)
                                           .Destination(WELL_KNOWN_NAME.GetName())
                                           .Parameter(fooTuple));
    }

    co_await serverConn->Close();
    co_await clientConn->Close();
  };

  boost::asio::co_spawn(ioContext, work(), boost::asio::detached);

  ioContext.run();
}

BENCHMARK(CxxBus_EmptyMessage)->UseRealTime()->MeasureProcessCPUTime();
BENCHMARK(CxxBus_StringMessage)->UseRealTime()->MeasureProcessCPUTime();
BENCHMARK(CxxBus_BigStringMessage)->UseRealTime()->MeasureProcessCPUTime();
BENCHMARK(CxxBus_StructMessage)->UseRealTime()->MeasureProcessCPUTime();
BENCHMARK(CxxBus_NestedStructMessage)->UseRealTime()->MeasureProcessCPUTime();
