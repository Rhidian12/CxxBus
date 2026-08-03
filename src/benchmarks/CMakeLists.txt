# ./src/benchmarks/CMakeListst.txt

define_test_or_benchmark(NAME benchmark_serialisation
                  SOURCES benchmark_serialisation.cpp
                  LIBRARIES cxxbus-lib
                  INCLUDE_DIRS ${cxxbus-include-dir})