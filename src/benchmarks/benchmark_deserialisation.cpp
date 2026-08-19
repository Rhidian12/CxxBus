#include <benchmark/benchmark.h>
#include <sys/types.h>

#include <cstdint>
#include <cstdlib>
#include <type_traits>

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

static void BM_NestedMapDeserialisation(benchmark::State& state)
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
  auto data = MarshalDBusType(map);

  for (auto _ : state)
  {
    UnmarshalDBusType<std::remove_cvref_t<decltype(map)>>(data, "a{ua{ua{ua{uu}}}}");
  }
}

static void BM_NestedStructDeserialisation(benchmark::State& state)
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

  auto data = MarshalDBusType(vec);

  for (auto _ : state)
  {
    UnmarshalDBusType<std::remove_cvref_t<decltype(vec)>>(data, "a(a(a(iii)))");
  }
}

static void BM_ArrayDeserialisation(benchmark::State& state)
{
  std::vector<uint64_t> vec;
  for (uint32_t i{}; i < 100'000; ++i)
  {
    vec.push_back(i);
  }

  auto data = MarshalDBusType(vec);

  for (auto _ : state)
  {
    UnmarshalDBusType<std::remove_cvref_t<decltype(vec)>>(data, "at");
  }
}

BENCHMARK(BM_NestedMapDeserialisation);
BENCHMARK(BM_NestedStructDeserialisation);
BENCHMARK(BM_ArrayDeserialisation);

BENCHMARK_MAIN();
