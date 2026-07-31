# ./src/test/CMakeListst.txt

define_unit_test(NAME test_marshalling
                  SOURCES test_marshalling.cpp
                  LIBRARIES cxxbus-lib
                  INCLUDE_DIRS ${cxxbus-include-dir})


define_unit_test(NAME test_unmarshalling
                  SOURCES test_unmarshalling.cpp
                  LIBRARIES cxxbus-lib
                  INCLUDE_DIRS ${cxxbus-include-dir})

define_unit_test(NAME test_message
                  SOURCES test_message.cpp
                  LIBRARIES cxxbus-lib
                  INCLUDE_DIRS ${cxxbus-include-dir})

define_unit_test(NAME test_dbus
                  SOURCES test_dbus.cpp
                  LIBRARIES cxxbus-lib
                  INCLUDE_DIRS ${cxxbus-include-dir})

define_unit_test(NAME test_dbus_sync
                  SOURCES test_dbus_sync.cpp
                  LIBRARIES cxxbus-lib
                  INCLUDE_DIRS ${cxxbus-include-dir})
