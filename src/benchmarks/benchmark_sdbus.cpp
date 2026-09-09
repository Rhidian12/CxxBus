#include <benchmark/benchmark.h>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IObject.h>
#include <sdbus-c++/Message.h>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/VTableItems.h>
#include <sdbus-c++/sdbus-c++.h>
#include <sys/types.h>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <cstdint>
#include <cstdlib>

static void BM_EmptyMessage(benchmark::State& state)
{
  auto proxy = sdbus::createProxy(sdbus::ServiceName{"org.cxxbus.test"}, sdbus::ObjectPath{"/org/cxxbus/test"});

  auto conn = sdbus::createSessionBusConnection(sdbus::ServiceName{"org.cxxbus.test"});
  auto obj = sdbus::createObject(*conn, sdbus::ObjectPath{"/org/cxxbus/test"});

  obj->addVTable(sdbus::MethodVTableItem{sdbus::MethodName{"Benchmark"},
                                         sdbus::Signature{""},
                                         {},
                                         {},
                                         {},
                                         [](sdbus::MethodCall call) { call.createReply().send(); },
                                         {}})
      .forInterface("org.cxxbus.test");

  conn->enterEventLoopAsync();

  for (auto _ : state)
  {
    auto method = proxy->createMethodCall(sdbus::InterfaceName{"org.cxxbus.test"}, sdbus::MethodName{"Benchmark"});
    proxy->callMethod(method);
  }

  conn->releaseName(sdbus::ServiceName{"org.cxxbus.test"});
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

  auto proxy = sdbus::createProxy(sdbus::ServiceName{"org.cxxbus.test"}, sdbus::ObjectPath{"/org/cxxbus/test"});

  auto conn = sdbus::createSessionBusConnection(sdbus::ServiceName{"org.cxxbus.test"});
  auto obj = sdbus::createObject(*conn, sdbus::ObjectPath{"/org/cxxbus/test"});

  obj->addVTable(sdbus::MethodVTableItem{sdbus::MethodName{"Benchmark"},
                                         sdbus::Signature{"a{ua{ua{ua{uu}}}}"},
                                         {},
                                         {},
                                         {},
                                         [](sdbus::MethodCall call) { call.createReply().send(); },
                                         {}})
      .forInterface("org.cxxbus.test");

  conn->enterEventLoopAsync();

  for (auto _ : state)
  {
    auto method = proxy->createMethodCall(sdbus::InterfaceName{"org.cxxbus.test"}, sdbus::MethodName{"Benchmark"});
    method << map;
    proxy->callMethod(method);
  }

  conn->releaseName(sdbus::ServiceName{"org.cxxbus.test"});
}

BENCHMARK(BM_EmptyMessage);
BENCHMARK(BM_NestedMapMessage);

BENCHMARK_MAIN();
