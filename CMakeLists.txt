# Root CMakeLists.txt

cmake_minimum_required(VERSION 3.27)

project(cxxbus VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  add_compile_options(/W4) # /Wx
else()
  add_compile_options(-Wall -Wextra) # -Werror
endif()

message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")

option(CXXBUS_ASAN "Build CxxBus with Address Sanitizer enabled." OFF)
option(CXXBUS_USAN "Build CxxBus with Undefined Behaviour Sanitizer enabled." OFF)
option(CXXBUS_TSAN "Build CxxBus with Thread Sanitizer enabled." OFF)
option(CXXBUS_SANITIZERS "Build CxxBus with ASAN and USAN enabled" OFF)
option(CXXBUS_ENABLE_TESTS "Build CxxBus Unit Tests" OFF)
option(CXXBUS_ENABLE_BENCHMARKS "Build CxxBus Benchmarks" OFF)
option(CXXBUS_DEV "Build CxxBus Unit Tests & Benchmarks" OFF)
set(CXXBUS_LOGLEVEL "ERROR" CACHE STRING "Set the CxxBus loglevel")

if(CXXBUS_SANITIZERS)
  set(CXXBUS_ASAN ON)
  set(CXXBUS_USAN ON)
endif()

if(CXXBUS_DEV)
  set(CXXBUS_ENABLE_TESTS ON)
  set(CXXBUS_ENABLE_BENCHMARKS ON)
endif()

message(STATUS "")
message(STATUS "============================================")
message(STATUS "             CxxBus Configuration           ")
message(STATUS "============================================")
message(STATUS "")
message(STATUS "  CXXBUS_ASAN: ${CXXBUS_ASAN}")
message(STATUS "  CXXBUS_USAN: ${CXXBUS_USAN}")
message(STATUS "  CXXBUS_SANITIZERS: ${CXXBUS_SANITIZERS}")
message(STATUS "  CXXBUS_TSAN: ${CXXBUS_TSAN}")
message(STATUS "  CXXBUS_ENABLE_TESTS: ${CXXBUS_ENABLE_TESTS}")
message(STATUS "  CXXBUS_ENABLE_BENCHMARKS: ${CXXBUS_ENABLE_BENCHMARKS}")
message(STATUS "  CXXBUS_DEV: ${CXXBUS_DEV}")
message(STATUS "  CXXBUS_LOGLEVEL: ${CXXBUS_LOGLEVEL}")
message(STATUS "")

if(CXXBUS_LOGLEVEL)
  add_compile_definitions(CXX_BUS_LOGLEVEL=${CXXBUS_LOGLEVEL})
endif()

if(CXXBUS_ASAN)
  message(STATUS "Enabling AddressSanitizer")

  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address -fno-omit-frame-pointer)
endif()

if(CXXBUS_USAN)
  message(STATUS "Enabling UndefinedBehaviorSanitizer")

  add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=undefined -fno-omit-frame-pointer)
endif()

if(CXXBUS_TSAN)
  message(STATUS "Enabling ThreadSanitizer")

  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread -fno-omit-frame-pointer)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include("build-system/UnitTests.cmake")
include("build-system/CreateBinary.cmake")

enable_testing()

if(CXXBUS_DEV)
  include(FetchContent)

  FetchContent_Declare(
  sdbus-c++
  GIT_REPOSITORY https://github.com/Kistler-Group/sdbus-cpp.git
  GIT_TAG        v2.0.0
)
  FetchContent_MakeAvailable(sdbus-c++)
endif()

add_subdirectory(thirdparty)
add_subdirectory(src)
