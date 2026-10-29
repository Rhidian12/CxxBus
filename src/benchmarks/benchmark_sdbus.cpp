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

sdbus::ServiceName const SERVICE_NAME = sdbus::ServiceName{"org.cxxbus.test"};
sdbus::ObjectPath const OBJECT_PATH = sdbus::ObjectPath{"/org/cxxbus/test"};
sdbus::InterfaceName const INTERFACE_NAME = sdbus::InterfaceName{"org.cxxbus.test"};
sdbus::MethodName const METHOD_NAME = sdbus::MethodName{"Benchmark"};

static void SDBus_EmptyMessage(benchmark::State& state)
{
  auto serverConn = sdbus::createSessionBusConnection(SERVICE_NAME);
  auto obj = sdbus::createObject(*serverConn, OBJECT_PATH);
  serverConn->enterEventLoopAsync();

  obj
      ->addVTable(sdbus::MethodVTableItem{
          METHOD_NAME, sdbus::Signature{""}, {}, {}, {}, [](sdbus::MethodCall call) { call.createReply().send(); }, {}})
      .forInterface(INTERFACE_NAME);

  auto clientConn = sdbus::createSessionBusConnection();
  auto proxy = sdbus::createProxy(*clientConn, SERVICE_NAME, OBJECT_PATH);

  for (auto _ : state)
  {
    auto method = proxy->createMethodCall(INTERFACE_NAME, METHOD_NAME);
    proxy->callMethod(method);
  }

  serverConn->releaseName(SERVICE_NAME);
}

static void SDBus_StringMessage(benchmark::State& state)
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
  for (int i{}; i < 100; ++i)
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

static void SDBus_BigStringMessage(benchmark::State& state)
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

static void SDBus_StructMessage(benchmark::State& state)
{
  auto serverConn = sdbus::createSessionBusConnection(SERVICE_NAME);
  auto obj = sdbus::createObject(*serverConn, OBJECT_PATH);

  obj->addVTable(sdbus::MethodVTableItem{METHOD_NAME,
                                         sdbus::Signature{"(sdiqb)"},
                                         {},
                                         {},
                                         {},
                                         [](sdbus::MethodCall call) { call.createReply().send(); },
                                         {}})
      .forInterface(INTERFACE_NAME);

  serverConn->enterEventLoopAsync();

  auto clientConn = sdbus::createSessionBusConnection();
  auto proxy = sdbus::createProxy(*clientConn, SERVICE_NAME, OBJECT_PATH);

  auto fooStruct =
      sdbus::Struct<std::string, double, int32_t, uint16_t, bool>{"Hello World!", 1.123456789, 987654321, 12345, true};

  for (auto _ : state)
  {
    auto method = proxy->createMethodCall(INTERFACE_NAME, METHOD_NAME);
    method << fooStruct;
    proxy->callMethod(method);
  }

  serverConn->releaseName(sdbus::ServiceName{"org.cxxbus.test"});
}

static void SDBus_NestedStructMessage(benchmark::State& state)
{
  auto serverConn = sdbus::createSessionBusConnection(SERVICE_NAME);
  auto obj = sdbus::createObject(*serverConn, OBJECT_PATH);

  obj->addVTable(sdbus::MethodVTableItem{METHOD_NAME,
                                         sdbus::Signature{"((sdiqb)(sdiqb)(sdiqb))"},
                                         {},
                                         {},
                                         {},
                                         [](sdbus::MethodCall call) { call.createReply().send(); },
                                         {}})
      .forInterface(INTERFACE_NAME);

  serverConn->enterEventLoopAsync();

  auto clientConn = sdbus::createSessionBusConnection();
  auto proxy = sdbus::createProxy(*clientConn, SERVICE_NAME, OBJECT_PATH);

  using FooStruct = sdbus::Struct<std::string, double, int32_t, uint16_t, bool>;
  auto fooStruct = FooStruct{"Hello World!", 1.123456789, 987654321, 12345, true};
  using BarStruct = sdbus::Struct<FooStruct, FooStruct, FooStruct>;
  auto barStruct = BarStruct{fooStruct, fooStruct, fooStruct};

  for (auto _ : state)
  {
    auto method = proxy->createMethodCall(INTERFACE_NAME, METHOD_NAME);
    method << barStruct;
    proxy->callMethod(method);
  }

  serverConn->releaseName(sdbus::ServiceName{"org.cxxbus.test"});
}

BENCHMARK(SDBus_EmptyMessage)->UseRealTime()->MeasureProcessCPUTime();
BENCHMARK(SDBus_StringMessage)->UseRealTime()->MeasureProcessCPUTime();
BENCHMARK(SDBus_BigStringMessage)->UseRealTime()->MeasureProcessCPUTime();
BENCHMARK(SDBus_StructMessage)->UseRealTime()->MeasureProcessCPUTime();
BENCHMARK(SDBus_NestedStructMessage)->UseRealTime()->MeasureProcessCPUTime();
