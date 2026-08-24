#include <benchmark/benchmark.h>
#include <sys/types.h>

#include <cstdint>
#include <cstdlib>

#include "src/DBus.h"

using namespace cxxbus;

struct InnerStruct
{
  int a;
  int b;
  int c;
};

struct OuterStruct
{
  std::vector<InnerStruct> inners;
};

struct OuterOuterStruct
{
  std::vector<OuterStruct> outers;
};

static void BM_NestedMapSerialisation(benchmark::State& state)
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
  for (auto _ : state)
  {
    MarshalDBusType(map);
  }
}

static void BM_NestedStructSerialisation(benchmark::State& state)
{
  std::vector<std::tuple<std::vector<std::tuple<std::vector<std::tuple<int, int, int>>>>>> vec{};
  for (uint32_t i{}; i < 50; ++i)
  {
    std::vector<std::tuple<std::vector<std::tuple<int, int, int>>>> outerOuter;
    for (uint32_t j{}; j < 25; ++j)
    {
      std::vector<std::tuple<int, int, int>> outer;
      for (uint32_t k{}; k < 12; ++k)
      {
        outer.push_back({rand() % 100, rand() % 100, rand() % 100});
      }
      outerOuter.push_back(outer);
    }
    vec.push_back(outerOuter);
  }

  for (auto _ : state)
  {
    MarshalDBusType(vec);
  }
}

static void BM_ArraySerialisation(benchmark::State& state)
{
  std::vector<uint64_t> vec;
  for (uint32_t i{}; i < 100'000; ++i)
  {
    vec.push_back(i);
  }

  for (auto _ : state)
  {
    MarshalDBusType(vec);
  }
}

static void BM_StringSerialisation(benchmark::State& state)
{
  std::string str{};
  for (int i{}; i < 10'000; ++i)
  {
    str.push_back(i % 255);
  }

  for (auto _ : state)
  {
    MarshalDBusType(str);
  }
}

static void BM_IntegerSerialisation(benchmark::State& state)
{
  for (auto _ : state)
  {
    MarshalDBusType(static_cast<uint64_t>(4851335));
  }
}

static void BM_DoubleSerialisation(benchmark::State& state)
{
  for (auto _ : state)
  {
    MarshalDBusType(static_cast<double>(3.1415161718));
  }
}

BENCHMARK(BM_NestedMapSerialisation);
BENCHMARK(BM_NestedStructSerialisation);
BENCHMARK(BM_ArraySerialisation);
BENCHMARK(BM_StringSerialisation);
BENCHMARK(BM_IntegerSerialisation);
BENCHMARK(BM_DoubleSerialisation);
