# CxxBus

DBus implementation in C++

Run tests:
```
clang++ -std=c++23 test_marshalling.cpp DBusMessage.cpp DBusConnection.cpp DBusReply.cpp DBusTypes.cpp DBusHelpers.cpp -O0 -g -lpthread -lgtest -lgtest_main

clang++ -std=c++23 test_unmarshalling.cpp DBusMessage.cpp DBusConnection.cpp DBusReply.cpp DBusTypes.cpp DBusHelpers.cpp -O0 -g -lpthread -lgtest -lgtest_main
```