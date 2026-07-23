# ./src CMakeLists.txt

# Clear cache
set(_ALL_TESTS_SOURCES   "" CACHE INTERNAL "")
set(_ALL_TESTS_LIBRARIES "" CACHE INTERNAL "")
set(_ALL_TESTS_INCLUDES  "" CACHE INTERNAL "")
set(_ALL_TESTS_NAMES     "" CACHE INTERNAL "")

add_library(cxxbus-lib  DBusConnection.cpp
                        DBusHelpers.cpp
                        DBusMessage.cpp
                        DBusReply.cpp
                        DBusTypes.cpp)

set(cxxbus-include-dir ${CMAKE_SOURCE_DIR})

add_subdirectory(test)

# Build the combined executable if we registered any unit tests
if(_ALL_TESTS_SOURCES)
  define_unit_test(NAME run_all_tests
                    SOURCES ${_ALL_TESTS_SOURCES}
                    LIBRARIES ${_ALL_TESTS_LIBRARIES}
                    INCLUDE_DIRS ${_ALL_TESTS_INCLUDES}
                    TARGET all_tests
                    DO_NOT_ADD_TO_CACHE)
endif()
