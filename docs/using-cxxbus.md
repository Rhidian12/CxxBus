# Using the CxxBus library

## Table Of Contents

1. [Asynchronous DBus Connections](#asynchronousdbusconnection)
    1. [Creating the DBus Connection](#creating-dbus-connection)
    1. [Calling a method](#calling-a-method)
    1. [Receiving incoming messages](#receiving-incoming-messages)

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
  DBus Methods require a Method name, DBus Destination and DBus Object Path, so these are 3 required parameters in the `Method` call.
- A normal outgoing signal emission, created by `DBusMessage::Signal()`
  DBus Signals require a Signal name, the DBus Interface the signal is emitted from and the object path the signal is emitted from.

```cpp
using namespace cxxbus;

// Send a message and await a reply from the other side.
// Our method is `Foo`, the destination is `com.dbus.exampleserver` and the object path is `/foo`
co_await conn->SendMessage(DBusMessage::Method("Foo", "com.dbus.exampleserver", ObjectPath{"/foo"}));

// Important! Signals NEVER expect a reply, so using `SendMessage()` will cause your code to infinitely block
// Our signal is called 'Bar', our interface is called `com.dbus.exampleclient` and we're invoking it from object path `/client`
co_await conn->SendMessageNoReply(DBusMessage::Signal("Bar", DBusInterfaceName{"com.dbus.exampleclient"}, ObjectPath{"/client"}))

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

Receiving incoming messages
---------------------------

This section does not cover replies to earlier sent messages. Those are always automatically returned to the originating `SendMessage()` call.

There are multiple ways incoming messages can be received & handled. The following list is the order of how messages are processed.

1. If the message is a signal then the message is matched against any added match rules via `AddMatchRule()`
  If the incoming message matches the added rule it's callback is ran.
2. The incoming message is passed to all registered message filters added via `RegisterMessageFilter()`.
  If any filter returns `MessageHandled::YES` then no other filters are ran and the message is considered handled and
  no other processing on the message occurs.
3
