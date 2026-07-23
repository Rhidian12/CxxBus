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

# Helper function to define a CMake target for GTest unit tests
# Arguments:
# - DO_NOT_ADD_TO_CACHE: By default all listed sources, libraries and
#     include directories are added to the CMake Cache so we can later
#     create a executable that contains all defined unit tests.
#     If you don't want your unit test to be included in the overall executable,
#     pass the DO_NOT_ADD_TO_CACHE option
#
# - NAME: The name of your CMake Target
# - SOURCES: List of source files to be compiled for the unit test
# - LIBRARIES: List of libraries to be included for the unit test
# - INCLUDE_DIRS: List of include directories to be included for the unit test
function(define_unit_test)
  find_package(GTest REQUIRED)

  set(options DO_NOT_ADD_TO_CACHE)
  set(oneValueArgs NAME)
  set(multiValueArgs SOURCES LIBRARIES INCLUDE_DIRS)

  cmake_parse_arguments(DU
    "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  # Make the test sources a relative path for later usage
  add_current_source_prefix("${DU_SOURCES}" test_sources)

  # Add the unit tests as a stand-alone executable
  add_executable(${DU_NAME} EXCLUDE_FROM_ALL ${DU_SOURCES})
  target_link_libraries(${DU_NAME} PRIVATE GTest::gtest_main)
  target_link_libraries(${DU_NAME} PRIVATE ${DU_LIBRARIES})
  target_include_directories(${DU_NAME} PUBLIC ${DU_INCLUDE_DIRS})
  include(CTest)
  include(GoogleTest)
  gtest_discover_tests(${DU_NAME})

  if(NOT DO_NOT_ADD_TO_CACHE)
    # Combine all sources and libraries to create a
    # combined unit-test executable

    # First get the CACHE variables
    set(all_tests_sources ${_ALL_TESTS_SOURCES})
    set(all_tests_libraries ${_ALL_TESTS_LIBRARIES})
    set(all_tests_includes ${_ALL_TESTS_INCLUDES})
    set(all_tests_names ${_ALL_TESTS_NAMES})

    # Get absolute paths
    make_absolute_paths(test_sources)

    # Then add our new data to them
    list(APPEND all_tests_sources ${test_sources})
    list(APPEND all_tests_libraries ${DU_LIBRARIES})
    list(APPEND all_tests_names build_${DU_NAME})
    list(APPEND all_tests_includes ${DU_INCLUDE_DIRS})

    # Then update the CACHE
    set(_ALL_TESTS_SOURCES       ${all_tests_sources}        CACHE INTERNAL "")
    set(_ALL_TESTS_LIBRARIES     ${all_tests_libraries}      CACHE INTERNAL "")
    set(_ALL_TESTS_INCLUDES      ${all_tests_includes}       CACHE INTERNAL "")
    set(_ALL_TESTS_NAMES         ${all_tests_names}          CACHE INTERNAL "")
  endif()

endfunction()
