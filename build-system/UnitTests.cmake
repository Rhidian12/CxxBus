function(make_absolute_paths paths)
  set(new_paths)
  foreach(path ${${paths}})
    cmake_path(ABSOLUTE_PATH path OUTPUT_VARIABLE new_path)
    list(APPEND new_paths ${new_path})
  endforeach()

  set(${paths} ${new_paths} PARENT_SCOPE)
endfunction()

function(add_current_source_prefix paths out_var)
  set(new_paths)
  foreach(source ${paths})
    list(APPEND new_paths ${CMAKE_CURRENT_SOURCE_DIR}/${source})
  endforeach()

  set(${out_var} ${new_paths} PARENT_SCOPE)
endfunction()

# Helper function to define a CMake target for GTest unit tests and Google Benchmarks
# Arguments:
# - DO_NOT_ADD_TO_CACHE: By default all listed sources, libraries and
#     include directories are added to the CMake Cache so we can later
#     create a executable that contains all defined unit tests.
#     If you don't want your unit test to be included in the overall executable,
#     pass the DO_NOT_ADD_TO_CACHE option
#
# - UNIT_TEST: Is this a unit test, or a Google Benchmark target?
#
# - NAME: The name of your CMake Target
# - SOURCES: List of source files to be compiled for the unit test
# - LIBRARIES: List of libraries to be included for the unit test
# - INCLUDE_DIRS: List of include directories to be included for the unit test
function(define_test_or_benchmark)
  set(options DO_NOT_ADD_TO_CACHE UNIT_TEST)
  set(oneValueArgs NAME)
  set(multiValueArgs SOURCES LIBRARIES INCLUDE_DIRS)

  cmake_parse_arguments(DU
    "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(DU_UNIT_TEST)
    find_package(GTest REQUIRED)
  else()
    find_package(benchmark REQUIRED)
  endif()

  # Make the test sources a relative path for later usage
  add_current_source_prefix("${DU_SOURCES}" sources)

  # Add the unit tests as a stand-alone executable
  add_executable(${DU_NAME} EXCLUDE_FROM_ALL ${DU_SOURCES})
  target_link_libraries(${DU_NAME} PRIVATE ${DU_LIBRARIES})
  target_include_directories(${DU_NAME} PUBLIC ${DU_INCLUDE_DIRS})

  if(DU_UNIT_TEST)
    target_link_libraries(${DU_NAME} PRIVATE GTest::gtest_main)
    include(CTest)
    include(GoogleTest)
    gtest_discover_tests(${DU_NAME})
  else()
    target_link_libraries(${DU_NAME} PRIVATE benchmark::benchmark_main)
  endif()

  if(NOT DO_NOT_ADD_TO_CACHE)
    # Combine all sources and libraries to create a
    # combined unit-test executable

    # First get the CACHE variables
    if(DU_UNIT_TEST)
      set(all_tests_sources   ${_ALL_TESTS_SOURCES})
      set(all_tests_libraries ${_ALL_TESTS_LIBRARIES})
      set(all_tests_includes  ${_ALL_TESTS_INCLUDES})
    else()
      set(all_benchmark_sources   ${_ALL_BENCHMARKS_SOURCES})
      set(all_benchmark_libraries ${_ALL_BENCHMARKS_LIBRARIES})
      set(all_benchmark_includes  ${_ALL_BENCHMARKS_INCLUDES})
    endif()

    # Get absolute paths
    make_absolute_paths(sources)

    # Then add our new data to them
    if(DU_UNIT_TEST)
      list(APPEND all_tests_sources   ${sources})
      list(APPEND all_tests_libraries ${DU_LIBRARIES})
      list(APPEND all_tests_includes  ${DU_INCLUDE_DIRS})
    else()
      list(APPEND all_benchmarks_sources   ${sources})
      list(APPEND all_benchmarks_libraries ${DU_LIBRARIES})
      list(APPEND all_benchmarks_includes  ${DU_INCLUDE_DIRS})
    endif()

    # Then update the CACHE
    if(DU_UNIT_TEST)
      set(_ALL_TESTS_SOURCES       ${all_tests_sources}        CACHE INTERNAL "")
      set(_ALL_TESTS_LIBRARIES     ${all_tests_libraries}      CACHE INTERNAL "")
      set(_ALL_TESTS_INCLUDES      ${all_tests_includes}       CACHE INTERNAL "")
    else()
      set(_ALL_BENCHMARKS_SOURCES       ${all_benchmarks_sources}        CACHE INTERNAL "")
      set(_ALL_BENCHMARKS_LIBRARIES     ${all_benchmarks_libraries}      CACHE INTERNAL "")
      set(_ALL_BENCHMARKS_INCLUDES      ${all_benchmarks_includes}       CACHE INTERNAL "")
    endif()
  endif()

endfunction()
