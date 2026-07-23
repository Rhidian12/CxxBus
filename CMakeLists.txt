# Root CMakeLists.txt

cmake_minimum_required(VERSION 4.3)

project(cxxbus VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")

if(MSVC)
  add_compile_options(/W4) # /Wx
else()
  add_compile_options(-Wall -Wextra) # -Werror
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include("build-system/UnitTests.cmake")
include("build-system/CreateBinary.cmake")

enable_testing()

add_subdirectory(src)
