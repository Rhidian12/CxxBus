# ./src CMakeLists.txt

# Clear cache
set(_ALL_TESTS_SOURCES   "" CACHE INTERNAL "")
set(_ALL_TESTS_LIBRARIES "" CACHE INTERNAL "")
set(_ALL_TESTS_INCLUDES  "" CACHE INTERNAL "")
set(_ALL_BENCHMARKS_SOURCES   "" CACHE INTERNAL "")
set(_ALL_BENCHMARKS_LIBRARIES "" CACHE INTERNAL "")
set(_ALL_BENCHMARKS_INCLUDES  "" CACHE INTERNAL "")

add_library(cxxbus-lib  DBusConnection.cpp
                        DBusHelpers.cpp
                        DBusMessage.cpp
                        IncomingDBusMessage.cpp
                        DBusTypes.cpp
                        Log.cpp
                        DBusMatchRule.cpp
                        DBusNameCache.cpp
                        SyncDBusConnection.cpp)

target_include_directories(cxxbus-lib PUBLIC ${thirdparty-include-dir})

set(cxxbus-include-dir ${CMAKE_SOURCE_DIR})

create_binary(NAME cxxbus-test
              SOURCES main.cpp
              LIBRARIES cxxbus-lib
              INCLUDE_DIRS ${cxxbus-include-dir})

create_binary(NAME cxxbus-test-sync
              SOURCES main_sync.cpp
              LIBRARIES cxxbus-lib
              INCLUDE_DIRS ${cxxbus-include-dir})

add_subdirectory(test)
add_subdirectory(benchmarks)

# Build the combined executable if we registered any unit tests
if(_ALL_TESTS_SOURCES)
  define_test_or_benchmark(NAME run_all_tests
                    SOURCES ${_ALL_TESTS_SOURCES}
                    LIBRARIES ${_ALL_TESTS_LIBRARIES}
                    INCLUDE_DIRS ${_ALL_TESTS_INCLUDES}
                    DO_NOT_ADD_TO_CACHE
                    UNIT_TEST)
endif()

# Build the combined executable if we registered any benchmarks
if(_ALL_BENCHMARKS_SOURCES)
  define_test_or_benchmark(NAME run_all_benchmarks
                    SOURCES ${_ALL_BENCHMARKS_SOURCES}
                    LIBRARIES ${_ALL_BENCHMARKS_LIBRARIES}
                    INCLUDE_DIRS ${_ALL_BENCHMARKS_INCLUDES}
                    DO_NOT_ADD_TO_CACHE
                    UNIT_TEST)
endif()