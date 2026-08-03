#include <benchmark/benchmark.h>

#include <cstdlib>

#include "src/DBus.h"

using namespace cxxbus;

struct InnerStruct
{
  uint32_t a;
  uint32_t b;
  uint32_t c;
};

struct OuterStruct
{
  std::vector<InnerStruct> inners;
};

struct DetailedPlateInfo
{
  std::vector<OuterStruct> outers;
};

static void BM_NestedMapSerialisation(benchmark::State& state)
{
  std::map<uint32_t, std::map<uint32_t, std::map<uint32_t, std::map<uint32_t, uint32_t>>>> nestedMap;
  for (uint32_t i{}; i < 50; ++i)
  {
    for (uint32_t j{}; j < 25; ++j)
    {
      for (uint32_t k{}; k < 12; ++k)
      {
        for (uint32_t l{}; l < 6; ++l)
        {
          nestedMap[i][j][k][l] = rand() % 100;
        }
      }
    }
  }

  for (auto _ : state)
  {
    MarshalDBusType(nestedMap);
  }
}

BENCHMARK(BM_NestedMapSerialisation);

BENCHMARK_MAIN();