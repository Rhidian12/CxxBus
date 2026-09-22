# ./src/benchmarks/CMakeListst.txt

define_test_or_benchmark(NAME benchmark_serialisation
                  SOURCES benchmark_serialisation.cpp
                  LIBRARIES cxxbus-lib
                  INCLUDE_DIRS ${cxxbus-include-dir})

define_test_or_benchmark(NAME benchmark_deserialisation
                  SOURCES benchmark_deserialisation.cpp
                  LIBRARIES cxxbus-lib
                  INCLUDE_DIRS ${cxxbus-include-dir})

define_test_or_benchmark(NAME benchmark_message
                  SOURCES benchmark_message.cpp
                  LIBRARIES cxxbus-lib
                  INCLUDE_DIRS ${cxxbus-include-dir})


define_test_or_benchmark(NAME benchmark_cxxbus
                  SOURCES benchmark_cxxbus.cpp
                  LIBRARIES cxxbus-lib
                  INCLUDE_DIRS ${cxxbus-include-dir})

define_test_or_benchmark(NAME benchmark_sdbus
                  SOURCES benchmark_sdbus.cpp
                  LIBRARIES cxxbus-lib sdbus-c++
                  INCLUDE_DIRS ${cxxbus-include-dir})
