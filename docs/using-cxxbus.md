# Using the CxxBus library

## Table Of Contents

1. [Asynchronous DBus Connections](#asynchronousdbusconnection)
    1. [Creating the DBus Connection](#creating-dbus-connection)
    1. [Calling a method](#calling-a-method)
    1. [Receiving & handling incoming messages](#receiving-incoming-messages)
    1. [Matching signals](#matching-signals)
    1. [Closing the connection](#closing-the-connection)
2. [Synchronous DBus API](#synchronous-dbus-api)

Asynchronous DBus Connections
-----------------------------

Creating the DBus Connection
----------------------------

All functionality in CxxBus is centered around the `DBusConnection` class. It is both a client to other DBus Servers and can serve as a DBus server as well.
To create a `DBusConnection`, use the provided `DBusConnection::Create()` or `DBusConnection::CreateDetached()` methods:

```cpp
using namespace cxxbus;
boost::asio::io_context ioContext{};

// Upon connecting to the dbus-daemon, try to claim the well-known name `com.cxxbus.example`
std::shared_ptr<DBusConnection> conn = co_await DBusConnection::Create(ioContext, 
                                                                       DBusWellKnownName{"com.cxxbus.example"}, BusType::SESSION);

// Upon connecting to the dbus-daemon, do not claim any well-known name.
// Useful for when your connection is not intended to receive incoming traffic except replies
std::shared_ptr<DBusConnection> conn2 = co_await DBusConnection::Create(ioContext, 
                                                                        std::nullopt, BusType::SESSION);

// Connect to the system-bus instead of the session-bus.
std::shared_ptr<DBusConnection> conn3 = co_await DBusConnection::Create(ioContext, 
                                                                        std::nullopt, BusType::SYSTEM);

// Connect without awaiting the connection being created. The provided callback will be fired when 
// the connection is established.
// All previous examples also apply to `CreateDetached()`
std::shared_ptr<DBusConnection> conn4 = DBusConnection::CreateDetached(ioContext,
                                                                       DBusWellKnownName{"com.cxxbus.detachedexample"},
                                                                       []() -> boost::asio::awaitable<void> { std::cout << "Connected!"; },
                                                                       BusType::SESSION);
```

Trying to send messages on the DBus Connection without awaiting the connection is well-defined: Messages are internally queued until the connection is established
and then processed.

Calling a method
----------------

Once your connection has been created, you can call functions by using the `SendMessage()` and `SendMessageNoReply()` functions and creating a `DBusMessage`.
An outgoing `DBusMessage` has 2 potential forms:

- A normal outgoing method call, created by `DBusMessage::Method()`
  DBus Methods require a Method name and DBus Object Path, so these are 2 required parameters in the `Method` call.
  The destination is not required but *highly* recommended if you're not sure what you're doing
- A normal outgoing signal emission, created by `DBusMessage::Signal()`
  DBus Signals require a Signal name, the DBus Interface the signal is emitted from and the object path the signal is emitted from.

```cpp
using namespace cxxbus;

// Send a message and await a reply from the other side.
// Our method is `Foo`, the destination is `com.dbus.exampleserver` and the object path is `/foo`
co_await conn->SendMessage(DBusMessage::Method("Foo", ObjectPath{"/foo"}).Destination("com.dbus.exampleserver"));

// Important! Signals NEVER expect a reply, so using `SendMessage()` will cause your code to infinitely block
// Our signal is called 'Bar', our interface is called `com.dbus.exampleclient` and we're invoking it from object path `/client`
co_await conn->SendMessageNoReply(DBusMessage::Signal("Bar", ObjectPath{"/client"}, DBusInterfaceName{"com.dbus.exampleclient"}))

// Our method is `FooBar`, and we're passing a DBus struct along as a parameter for the `FooBar` method by 
// chaining the `.Parameter()` method into the `DBusMessage::Method()` method
// The same pattern works for *any* kind of `DBusMessage`
IncomingDBusMessage reply = co_await conn->SendMessage(DBusMessage::Method(
                                                          "FooBar",
                                                          "com.dbus.exampleserver",
                                                          ObjectPath{"/foo"}).
                                                        Parameter(std::tuple<uint32_t, std::string>{42, "The Answer!"})
                                                       );

// Our `FooBar` method returns a string, so 
// we can extract that string by calling the `.Get()` function on the received reply
std::cout << "What is 42 to the answer to? " << reply.Get<std::string>() << "\n";
```

Every kind message is allowed to have any of these parameters and can be added as needed via the following `DBusMessage` API:

- `Path()` -> Adds an Object path to a message
- `Interface()` -> Adds an interface to a message
- `Destination()` -> Adds a destination to a message
- `Flag()` -> Sets a flag for this message.
  `SendMessageNoReply()` automatically adds the `DBusMessageFlags::NO_REPLY_EXPECTED` flag on messages.

Receiving & handling incoming messages
---------------------------

This section does not cover replies to earlier sent messages. Those are always automatically returned to the originating `SendMessage()` call.

There are multiple ways incoming messages can be received & handled. The following list is the order of how messages are processed.

1. If the message is a signal then the message is matched against any added match rules via `AddMatchRule()`
  If the incoming message matches the added rule it's callback is ran.
  No other processing on the message occurs if it's a signal.
2. The incoming message is passed to all registered message filters added via `RegisterMessageFilter()`.
  The filters are ran in the order they were added.
  If any filter returns `MessageHandled::YES` then no other filters are ran and the message is considered handled and
  no other processing on the message occurs.
3. The incoming message is passed to any object path handler added via `RegisterObjectPathHandler()` if the message's path matches the registered object path.
4. If previous steps did not handle the message, then the final fallback is to pass the message to handlers registered via `ReceiveIncomingMessages()`
  This is a general-case handler to receive *any* incoming message (as long as it's not a signal) regardless of interface, path, ...
5. If none of the above steps handled the message we return a DBus error of type `org.freedesktop.DBus.Error.UnknownMethod`.

Upon receiving a message, you should generally send a reply back if the sender is expecting a reply.

```cpp
using namespace cxxbus;

std::shared_ptr<DBusConnection> conn = co_await DBusConnection::Create(ioContext, 
                                                                       DBusWellKnownName{"com.dbus.exampleserver"}, BusType::SESSION);

// Accept incoming messages on the destination `com.dbus.exampleserver` (as defined above when we created the connection) that also 
// specifies the path `/foo`
co_await conn->RegisterObjectPathHandler(ObjectPath{"/foo"}, [conn](IncomingDBusMessage const & message) -> boost::asio::awaitable<void> {
  // If the method (= member in DBus terminology) is "The Answer!", send a reply back!
  if (*message.GetMember() == "The Answer!")
  {
    co_await conn->SendMessageNoReply(DBusMessage::Reply(message).Parameter("Everything!"));
  }
});
```

Matching signals
----------------

To match incoming signals, you can add [DBus Match Rules](https://dbus.freedesktop.org/doc/dbus-specification.html#:~:text=Match%20Rules,-An) via the `AddMatchRule()` function.

Closing the connection
----------------------

The `DBusConnection` should be closed at the end of the program by calling the provided `Close()` function.
This will kill the connection to the dbus-daemon and release any acquired well-known names.

Synchronous DBus API
--------------------

The `MultithreadedDBusConnection` supports both a synchronous and asynchronous API. The synchronous API is identical to the asynchronous API and is suffixed with `Sync`, e.g. `CreateSync()` instead of `Create()`.

There is no possibility of using a fully synchronous DBus API without relying on Boost.Asio or disabling the asynchronous API.

CxxBus is asynchronous-first and any synchronous support is second-class, but should work.
