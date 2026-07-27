# CxxBus

DBus implementation in C++

The project is built via CMake:

```md
# Configure
cmake -S . -B build

# Configure in DEBUG
cmake -DCMAKE_BUILD_TYPE=Debug -S . -B build

# Configure a specific C++ compiler:
cmake -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -S . -B build

# To run all test cases
cmake --build build --target run_all_tests

# To set the logging level (Error by default)
cmake -DCMAKE_CXX_FLAGS="-DCXX_BUS_LOGLEVEL=Fatal" -S . -B build
```
