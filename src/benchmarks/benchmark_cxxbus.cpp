#include <benchmark/benchmark.h>
#include <sys/types.h>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <cstdint>
#include <cstdlib>

#include "src/DBusConnection.h"
#include "src/DBusMessage.h"
#include "src/DBusTypes.h"
#include "src/IncomingDBusMessage.h"

static void BM_EmptyMessage(benchmark::State& state)
{
  boost::asio::io_context ioContext{};
  auto work = [&ioContext, &state]() -> boost::asio::awaitable<void>
  {
    auto conn = co_await cxxbus::DBusConnection::Create(ioContext, cxxbus::DBusWellKnownName{"org.cxxbus.test2"},
                                                        cxxbus::BusType::SESSION);

    conn->RegisterObjectPathHandler(cxxbus::ObjectPath{"/org/cxxbus/test"},
                                    [conn](cxxbus::IncomingDBusMessage msg) -> boost::asio::awaitable<void>
                                    { co_return co_await conn->SendMessageNoReply(cxxbus::DBusMessage::Reply(msg)); });

    for (auto _ : state)
    {
      co_await conn->SendMessage(cxxbus::DBusMessage::Method("Benchmark")
                                     .Destination("org.cxxbus.test2")
                                     .Path(cxxbus::ObjectPath{"/org/cxxbus/test"}));
    }

    co_await conn->Close();
  };

  boost::asio::co_spawn(ioContext, work(), boost::asio::detached);

  ioContext.run();
}

static void BM_StringMessage(benchmark::State& state)
{
  boost::asio::io_context ioContext{};
  auto work = [&ioContext, &state]() -> boost::asio::awaitable<void>
  {
    auto conn = co_await cxxbus::DBusConnection::Create(ioContext, cxxbus::DBusWellKnownName{"org.cxxbus.test2"},
                                                        cxxbus::BusType::SESSION);

    conn->RegisterObjectPathHandler(cxxbus::ObjectPath{"/org/cxxbus/test"},
                                    [conn](cxxbus::IncomingDBusMessage msg) -> boost::asio::awaitable<void>
                                    { co_return co_await conn->SendMessageNoReply(cxxbus::DBusMessage::Reply(msg)); });

    std::string str{};
    for (int i{}; i < 10'000; ++i)
    {
      str.push_back(std::max(i % 127, 1));
    }

    for (auto _ : state)
    {
      co_await conn->SendMessage(cxxbus::DBusMessage::Method("Benchmark")
                                     .Destination("org.cxxbus.test2")
                                     .Path(cxxbus::ObjectPath{"/org/cxxbus/test"})
                                     .Parameter(str));
    }

    co_await conn->Close();
  };

  boost::asio::co_spawn(ioContext, work(), boost::asio::detached);

  ioContext.run();
}

static void BM_NestedMapMessage(benchmark::State& state)
{
  std::map<uint32_t, std::map<uint32_t, std::map<uint32_t, std::map<uint32_t, uint32_t>>>> map{};
  for (uint32_t plateNr{}; plateNr < 18; ++plateNr)
  {
    for (uint32_t bankNr{}; bankNr < 3; ++bankNr)
    {
      for (uint32_t powerLevel{}; powerLevel < 24; ++powerLevel)
      {
        for (uint32_t temperatureLevel{}; temperatureLevel < 32; ++temperatureLevel)
        {
          map[plateNr][bankNr][powerLevel][temperatureLevel] = rand() % 100;
        }
      }
    }
  }

  boost::asio::io_context ioContext{};
  auto work = [&ioContext, &state, &map]() -> boost::asio::awaitable<void>
  {
    auto conn = co_await cxxbus::DBusConnection::Create(ioContext, cxxbus::DBusWellKnownName{"org.cxxbus.test2"},
                                                        cxxbus::BusType::SESSION);

    conn->RegisterObjectPathHandler(cxxbus::ObjectPath{"/org/cxxbus/test"},
                                    [conn](cxxbus::IncomingDBusMessage msg) -> boost::asio::awaitable<void>
                                    { co_return co_await conn->SendMessageNoReply(cxxbus::DBusMessage::Reply(msg)); });

    for (auto _ : state)
    {
      co_await conn->SendMessage(cxxbus::DBusMessage::Method("Benchmark")
                                     .Destination("org.cxxbus.test2")
                                     .Path(cxxbus::ObjectPath{"/org/cxxbus/test"})
                                     .Parameter(map));
    }

    co_await conn->Close();
  };

  boost::asio::co_spawn(ioContext, work(), boost::asio::detached);

  ioContext.run();
}

BENCHMARK(BM_EmptyMessage);
BENCHMARK(BM_StringMessage);
BENCHMARK(BM_NestedMapMessage);
