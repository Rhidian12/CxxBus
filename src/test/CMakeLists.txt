# ./src/test/CMakeListst.txt

define_test_or_benchmark(NAME test_marshalling
                  SOURCES test_marshalling.cpp
                  LIBRARIES cxxbus-lib
                  INCLUDE_DIRS ${cxxbus-include-dir}
                  UNIT_TEST)


define_test_or_benchmark(NAME test_unmarshalling
                  SOURCES test_unmarshalling.cpp
                  LIBRARIES cxxbus-lib
                  INCLUDE_DIRS ${cxxbus-include-dir}
                  UNIT_TEST)

define_test_or_benchmark(NAME test_message
                  SOURCES test_message.cpp
                  LIBRARIES cxxbus-lib
                  INCLUDE_DIRS ${cxxbus-include-dir}
                  UNIT_TEST)

define_test_or_benchmark(NAME test_dbus
                  SOURCES test_dbus.cpp
                  LIBRARIES cxxbus-lib
                  INCLUDE_DIRS ${cxxbus-include-dir}
                  UNIT_TEST)

define_test_or_benchmark(NAME test_dbus_sync
                  SOURCES test_dbus_sync.cpp
                  LIBRARIES cxxbus-lib
                  INCLUDE_DIRS ${cxxbus-include-dir}
                  UNIT_TEST)
