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

include(FetchContent)

FetchContent_Declare(
  sdbus-c++
  GIT_REPOSITORY https://github.com/Kistler-Group/sdbus-cpp.git
  GIT_TAG        v2.0.0
)
FetchContent_MakeAvailable(sdbus-c++)

define_test_or_benchmark(NAME benchmark_sdbus
                  SOURCES benchmark_sdbus.cpp
                  LIBRARIES cxxbus-lib SDBusCpp::sdbus-c++
                  INCLUDE_DIRS ${cxxbus-include-dir})
#target_link_libraries(benchmark_sdbus PRIVATE SDBusCpp::sdbus-c++)
