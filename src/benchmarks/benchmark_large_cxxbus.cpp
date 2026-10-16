#include <benchmark/benchmark.h>

#include "src/DBusConnection.h"
#include "src/DBusMessage.h"
#include "src/DBusTypes.h"
#include "src/IncomingDBusMessage.h"

cxxbus::DBusWellKnownName const WELL_KNOWN_NAME = cxxbus::DBusWellKnownName{"org.cxxbus.test"};
cxxbus::ObjectPath const OBJECT_PATH = cxxbus::ObjectPath{"/org/cxxbus/test"};
std::string const METHOD_NAME = "Benchmark";

static void CxxBus_NestedMapMessage(benchmark::State& state)
{
  std::map<uint32_t, std::map<uint32_t, std::map<uint32_t, std::map<uint32_t, uint32_t>>>> map{};
  for (uint32_t i{}; i < 18; ++i)
  {
    for (uint32_t j{}; j < 3; ++j)
    {
      for (uint32_t k{}; k < 24; ++k)
      {
        for (uint32_t l{}; l < 32; ++l)
        {
          map[i][j][k][l] = rand() % 100;
        }
      }
    }
  }

  boost::asio::io_context ioContext{};
  auto work = [&ioContext, &state, &map]() -> boost::asio::awaitable<void>
  {
    auto serverConn = co_await cxxbus::DBusConnection::Create(ioContext, WELL_KNOWN_NAME, cxxbus::BusType::SESSION);
    auto clientConn = co_await cxxbus::DBusConnection::Create(ioContext, std::nullopt, cxxbus::BusType::SESSION);

    co_await serverConn->RegisterObjectPathHandler(
        OBJECT_PATH, [serverConn](cxxbus::IncomingDBusMessage const& msg) -> boost::asio::awaitable<void>
        { co_return co_await serverConn->SendMessageNoReply(cxxbus::DBusMessage::Reply(msg)); });

    for (auto _ : state)
    {
      co_await clientConn->SendMessage(
          cxxbus::DBusMessage::Method(METHOD_NAME, OBJECT_PATH).Destination(WELL_KNOWN_NAME.GetName()).Parameter(map));
    }

    co_await serverConn->Close();
    co_await clientConn->Close();
  };

  boost::asio::co_spawn(ioContext, work(), boost::asio::detached);

  ioContext.run();
}

static void CxxBus_MassiveArray(benchmark::State& state)
{
  boost::asio::io_context ioContext{};
  auto work = [&ioContext, &state]() -> boost::asio::awaitable<void>
  {
    auto serverConn = co_await cxxbus::DBusConnection::Create(ioContext, WELL_KNOWN_NAME, cxxbus::BusType::SESSION);
    auto clientConn = co_await cxxbus::DBusConnection::Create(ioContext, std::nullopt, cxxbus::BusType::SESSION);

    co_await serverConn->RegisterObjectPathHandler(
        OBJECT_PATH, [serverConn](cxxbus::IncomingDBusMessage const& msg) -> boost::asio::awaitable<void>
        { co_return co_await serverConn->SendMessageNoReply(cxxbus::DBusMessage::Reply(msg)); });

    std::vector<uint8_t> data;
    // 1MB of data
    for (uint32_t i{}; i < 1024 * 1024; ++i)
    {
      data.push_back(rand() % 200);
    }

    for (auto _ : state)
    {
      co_await clientConn->SendMessage(
          cxxbus::DBusMessage::Method(METHOD_NAME, OBJECT_PATH).Destination(WELL_KNOWN_NAME.GetName()).Parameter(data));
    }

    co_await serverConn->Close();
    co_await clientConn->Close();
  };

  boost::asio::co_spawn(ioContext, work(), boost::asio::detached);

  ioContext.run();
}

BENCHMARK(CxxBus_NestedMapMessage)->UseRealTime()->MeasureProcessCPUTime();
BENCHMARK(CxxBus_MassiveArray)->UseRealTime()->MeasureProcessCPUTime();
