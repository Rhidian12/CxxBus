# CxxBus

CxxBus is a highly performant low-level DBus implementation written in C++20 and is meant as an alternative to SDBus and libdbus.

## Overview

`CxxBus` is a Boost.Asio coroutine-based framework and therefore requires a Boost.Asio `io_context` on which to enqueue work.
There is limited support for a synchronous API with the `MultithreadedDBusConnection`, however it is far less performant and I can only highly recommend to use the async API.
Boost.Asio **is required for both** the asynchronous and synchronous API.

## Building

CxxBus is built using CMake:

```md
cmake -DCMAKE_BUILD_TYPE=Release -S . -B build
cmake --build build --target cxxbus-lib
```

Available targets are:

- `cxxbus-lib`
  Main build target. Builds the library
- `run_all_tests`
  Builds all unit tests. Only available if `CXXBUS_ENABLE_TESTS` is set
- `run_all_benchmarks`
  Builds all benchmarks. Only available if `CXXBUS_ENABLE_BENCHMARKS` is set
- `test_XXX`
  Every unit test is also separately available through a target `test_<UNIT_TEST_NAME>` where the name is the name of the file.
  e.g. `test_dbus.cpp` can be built via the `test_dbus` target
  Only available if `CXXBUS_ENABLE_TESTS` is set
- `benchmark_XXX`
  Every benchmark is also separately available through a target `benchmark_<BENCHMARK_NAME>` where the name is the name of the file.
  e.g. `benchmark_cxxbus.cpp` can be built via the `benchmark_cxxbus` target.
  Only available if `CXXBUS_ENABLE_BENCHMARKS` is set

### Configuration flags

- `CXXBUS_ENABLE_TESTS` [boolean] [Default: OFF]
  Build unit tests. These are available via the target `run_all_tests` or per specific file found in `src/test`
- `CXXBUS_ENABLE_BENCHMARKS` [boolean] [Default: OFF]
  Build the benchmarks. These are available via the target `run_all_benchmarks`, or per specific file found in `src/benchmarks`
- `CXXBUS_DEV` [boolean] [Default: OFF]
  quality-of-life flag that sets both `CXXBUS_ENABLE_TESTS` and `CXXBUS_ENABLE_BENCHMARKS`
- `CMAKE_BUILD_TYPE` [string]
  Set the CMake build type. Set to `Release` for maximum performance and production use. Set to `Debug` for debugging purposes.
- `CXXBUS_LOGLEVEL` [string] [Default: ERROR]
  Sets the logging level of the CxxBus library. Possible values: `TRACE`, `DEBUG`, `INFO`, `ERROR`, `OFF`.
  For production, leave this to `ERROR`, for debugging, set this to `TRACE` or `DEBUG`.
  By design, the log level `FATAL` will always be logged as this points to a failure in the library.
- `CXXBUS_USAN` [boolean] [Default: OFF]
  Enables a build with Undefined Behaviour Sanitizer. Do not use this in production as it harms performance.
- `CXXBUS_ASAN` [boolean] [Default: OFF]
  Enables a build with Address Sanitizer. Do not use this in production as it harms performance.
- `CXXBUS_TSAN` [boolean] [Default: OFF]
  Enables a build with Thread Sanitizer. Do not use this in production as it harms performance.
  Only works with Clang and cannot be combined with ASAN or USAN.
- `CXXBUS_SANITIZERS` [boolean] [Default: OFF]
  Quality-of-life flag that sets both `USAN` and `ASAN`. Do not use this in production as it harms performance.
  Cannot be combined with `TSAN`

## Requirements

- C++23 compiler, C++20 is partially supported, but prefer C++23.
- Boost.Asio available (CxxBus is verified to work with Boost.Asio 1.92.0-1)
- `GTest` for the unit tests
- `GoogleBenchmark` for the benchmarks

## Future work

- Support Unix Filedescriptors
- Add better support for integrating an external eventloop (better coroutine support, not only `boost::asio::awaitable`)
- More support for server addresses. Currently only `unix:path=` is supported
- Windows support
