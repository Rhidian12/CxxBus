# CxxBus

Low-level DBus implementation in C++

## Usage

There are 2 ways of using `CxxBus`:

- Via `boost::asio` and using its eventloop (recommended), or
- integrating your own eventloop (currently not recommended).

The base of `CxxBus` are the `DBusConnection` and `SyncDBusConnection` and are the core parts of `CxxBus` you will be interacting with.
Both of these provide the same functionality:

- Sending messages and waiting for a reply,
- Sending messages without waiting for replies (typically used for signals),
- Adding match rules to match incoming broadcast messages (typically signals)

Messages are easily created via the `DBusMessage` builder-style functions:

```cpp
using namespace cxxbus;

// A simple message containing a member (method) with a path, destination, interface and a string parameter attached.
DBusMessage message = DBusMessage::Method("Hello")
                                    .Path(ObjectPath{"/com/world/hello"}
                                    .Destination("com.world.Hello")
                                    .Interface(DBusInterfaceName{"com.world.Hello"})
                                    .Parameter(std::string{"Hello world!"});
```

A `DBusMessage` is created by first specifying the type of the message:

- `Method`,
- `Reply`,
- `Signal` or
- `Error`

After the type of the message, all parameters can be added as you want, however, per the DBus Spec, some messages require certain parameters.
`DBusConnection` and `SyncDBusConnection` will verify that the message you are sending contains the required fields.

Adding a parameter to a message is done via the `Parameter()` function which takes in any DBus-compatible type.
Note that you can only add **1** parameter to a `DBusMessage`. If you want to add more than 1 value as a parameter to your message, wrap your values in either:

- a `std::tuple` to make it a DBus struct (See [StructToTuple](https://github.com/Rhidian12/StructToTuple) for a library to automatically convert structs to tuples)
- a `MultipleCompleteTypes` to list multiple types after one another without it being a struct

## Building

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

# To enable sanitizers:
cmake -DASAN=1 -DUSAN=1 -S . -B build
cmake -DSANITIZERS=1 -S . -B build # Same as -DASAN=1 -DUSAN=1
cmake -DTSAN=1 -S . -B build
# Note that ASAN and TSAN cannot be combined
```

## Future work

- Support Unix Filedescriptors
- Add better support for integrating an external eventloop (better coroutine support, not only `boost::asio::awaitable`)
- Support the system bus instead of only the message bus
- More support for server addresses. Currently only `unix:path=` is supported
- Windows support
