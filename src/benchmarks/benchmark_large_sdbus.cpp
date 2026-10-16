#include <benchmark/benchmark.h>
#include <sdbus-c++/Error.h>
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

sdbus::ServiceName const SERVICE_NAME = sdbus::ServiceName{"org.cxxbus.test"};
sdbus::ObjectPath const OBJECT_PATH = sdbus::ObjectPath{"/org/cxxbus/test"};
sdbus::InterfaceName const INTERFACE_NAME = sdbus::InterfaceName{"org.cxxbus.test"};
sdbus::MethodName const METHOD_NAME = sdbus::MethodName{"Benchmark"};

static void SDBus_NestedMapMessage(benchmark::State& state)
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

  auto serverConn = sdbus::createSessionBusConnection(SERVICE_NAME);
  auto obj = sdbus::createObject(*serverConn, OBJECT_PATH);

  obj->addVTable(sdbus::MethodVTableItem{METHOD_NAME,
                                         sdbus::Signature{"a{ua{ua{ua{uu}}}}"},
                                         {},
                                         {},
                                         {},
                                         [](sdbus::MethodCall call) { call.createReply().send(); },
                                         {}})
      .forInterface(INTERFACE_NAME);

  serverConn->enterEventLoopAsync();

  auto clientConn = sdbus::createSessionBusConnection();
  auto proxy = sdbus::createProxy(*clientConn, SERVICE_NAME, OBJECT_PATH);

  for (auto _ : state)
  {
    auto method = proxy->createMethodCall(INTERFACE_NAME, METHOD_NAME);
    method << map;
    proxy->callMethod(method);
  }

  serverConn->releaseName(sdbus::ServiceName{"org.cxxbus.test"});
}

static void SDBus_MassiveArray(benchmark::State& state)
{
  auto serverConn = sdbus::createSessionBusConnection(SERVICE_NAME);
  auto obj = sdbus::createObject(*serverConn, OBJECT_PATH);

  obj->addVTable(sdbus::MethodVTableItem{METHOD_NAME,
                                         sdbus::Signature{"ay"},
                                         {},
                                         {},
                                         {},
                                         [](sdbus::MethodCall call) { call.createReply().send(); },
                                         {}})
      .forInterface(INTERFACE_NAME);

  serverConn->enterEventLoopAsync();

  auto clientConn = sdbus::createSessionBusConnection();
  auto proxy = sdbus::createProxy(*clientConn, SERVICE_NAME, OBJECT_PATH);

  std::vector<uint8_t> data;
  // 1MB of data
  for (uint32_t i{}; i < 1024 * 1024; ++i)
  {
    data.push_back(rand() % 200);
  }

  for (auto _ : state)
  {
    auto method = proxy->createMethodCall(INTERFACE_NAME, METHOD_NAME);
    method << data;
    proxy->callMethod(method);
  }

  serverConn->releaseName(sdbus::ServiceName{"org.cxxbus.test"});
}

BENCHMARK(SDBus_NestedMapMessage)->UseRealTime()->MeasureProcessCPUTime();
BENCHMARK(SDBus_MassiveArray)->UseRealTime()->MeasureProcessCPUTime();
