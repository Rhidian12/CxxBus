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

sdbus::ServiceName const SERVICE_NAME = sdbus::ServiceName{"org.cxxbus.test"};
sdbus::ObjectPath const OBJECT_PATH = sdbus::ObjectPath{"/org/cxxbus/test"};
sdbus::InterfaceName const INTERFACE_NAME = sdbus::InterfaceName{"org.cxxbus.test"};
sdbus::MethodName const METHOD_NAME = sdbus::MethodName{"Benchmark"};

static void BM_SDBusEmptyMessage(benchmark::State& state)
{
  auto serverConn = sdbus::createSessionBusConnection(SERVICE_NAME);
  auto obj = sdbus::createObject(*serverConn, OBJECT_PATH);

  obj
      ->addVTable(sdbus::MethodVTableItem{
          METHOD_NAME, sdbus::Signature{""}, {}, {}, {}, [](sdbus::MethodCall call) { call.createReply().send(); }, {}})
      .forInterface(INTERFACE_NAME);

  serverConn->enterEventLoopAsync();

  auto clientConn = sdbus::createSessionBusConnection();
  auto proxy = sdbus::createProxy(*clientConn, SERVICE_NAME, OBJECT_PATH);

  for (auto _ : state)
  {
    auto method = proxy->createMethodCall(INTERFACE_NAME, METHOD_NAME);
    proxy->callMethod(method);
  }

  serverConn->releaseName(SERVICE_NAME);
}

static void BM_SDBusStringMessage(benchmark::State& state)
{
  auto serverConn = sdbus::createSessionBusConnection(SERVICE_NAME);
  auto obj = sdbus::createObject(*serverConn, OBJECT_PATH);

  obj->addVTable(sdbus::MethodVTableItem{METHOD_NAME,
                                         sdbus::Signature{"s"},
                                         {},
                                         {},
                                         {},
                                         [](sdbus::MethodCall call) { call.createReply().send(); },
                                         {}})
      .forInterface(INTERFACE_NAME);

  serverConn->enterEventLoopAsync();

  auto clientConn = sdbus::createSessionBusConnection();
  auto proxy = sdbus::createProxy(*clientConn, SERVICE_NAME, OBJECT_PATH);

  std::string str{};
  for (int i{}; i < 10'000; ++i)
  {
    str.push_back(std::max(i % 127, 1));
  }

  for (auto _ : state)
  {
    auto method = proxy->createMethodCall(INTERFACE_NAME, METHOD_NAME);
    method << str;
    proxy->callMethod(method);
  }

  serverConn->releaseName(sdbus::ServiceName{"org.cxxbus.test"});
}

static void BM_SDBusNestedMapMessage(benchmark::State& state)
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

BENCHMARK(BM_SDBusEmptyMessage);
BENCHMARK(BM_SDBusStringMessage);
BENCHMARK(BM_SDBusNestedMapMessage);
