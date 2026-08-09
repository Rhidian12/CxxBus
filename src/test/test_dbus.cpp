#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/basic_channel.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/detail/error_code.hpp>
#include <functional>

#include "src/DBusConnection.h"
#include "src/DBusMatchRule.h"
#include "src/DBusMessage.h"
#include "src/DBusTypes.h"
#include "src/IncomingDBusMessage.h"
#include "src/Log.h"

using namespace cxxbus;

Logger const LOGGER{.logLevel = LogLevel::Trace};

struct DBusConnectionTestSuite : ::testing::Test
{
 public:
  boost::asio::io_context ioService;
  std::function<boost::asio::awaitable<void>()> coroutineToRun;
  std::shared_ptr<DBusConnection> conn;

  void SetUp() override
  {
    // Setup code here.
  }

  void TearDown() override
  {
    EXPECT_NO_THROW(boost::asio::co_spawn(
        ioService,
        [this]() -> boost::asio::awaitable<void>
        {
          co_await coroutineToRun();
          LOGGER.LogTrace("Finished running coroutine");

          if (conn != nullptr)
          {
            LOGGER.LogTrace("Closing DBus connection");
            co_await conn->Close();
          }
        },
        [](std::exception_ptr e)
        {
          if (e) std::rethrow_exception(e);
        }));

    ioService.run();
  }
};

TEST_F(DBusConnectionTestSuite, TestConnectingToDBusDaemon)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    conn = co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, BusType::SESSION);
    EXPECT_TRUE(conn != nullptr);
  };
}

TEST_F(DBusConnectionTestSuite, TestDetectingLostConnectionToDBusDaemon)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    conn = co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, BusType::SESSION);
    EXPECT_TRUE(conn->IsConnected());

    bool disconnected{false};
    conn->OnDisconnected([&disconnected]() { disconnected = true; });

    // Simulate the dbus-daemon dying/killing our connection, without needing to spawn and kill a
    // real dbus-daemon process.
    conn->SimulateConnectionLoss();

    // Give the read loop a chance to notice the socket failure and report it.
    boost::asio::system_timer timer{ioService};
    timer.expires_after(std::chrono::milliseconds(100));
    co_await timer.async_wait(boost::asio::use_awaitable);

    EXPECT_TRUE(disconnected);
    EXPECT_FALSE(conn->IsConnected());
  };
}

TEST_F(DBusConnectionTestSuite, TestIntrospectingDBusDaemon)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    conn = co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, BusType::SESSION);
    auto reply = co_await conn->SendMessage(DBusMessage::Method("Introspect")
                                                .Path(ObjectPath{"/org/freedesktop/DBus"})
                                                .Interface(DBusInterfaceName{"org.freedesktop.DBus.Introspectable"})
                                                .Destination("org.freedesktop.DBus"));

    EXPECT_TRUE(reply.GetHeader().GetSignature().has_value());
    EXPECT_EQ(reply.GetHeader().GetSignature().value(), Signature("s"));
    EXPECT_TRUE(reply.HasArguments());
    EXPECT_EQ(reply.Get<std::string>(),
              R"(<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN"
"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node>
  <interface name="org.freedesktop.DBus">
    <method name="Hello">
      <arg direction="out" type="s"/>
    </method>
    <method name="RequestName">
      <arg direction="in" type="s"/>
      <arg direction="in" type="u"/>
      <arg direction="out" type="u"/>
    </method>
    <method name="ReleaseName">
      <arg direction="in" type="s"/>
      <arg direction="out" type="u"/>
    </method>
    <method name="StartServiceByName">
      <arg direction="in" type="s"/>
      <arg direction="in" type="u"/>
      <arg direction="out" type="u"/>
    </method>
    <method name="UpdateActivationEnvironment">
      <arg direction="in" type="a{ss}"/>
    </method>
    <method name="NameHasOwner">
      <arg direction="in" type="s"/>
      <arg direction="out" type="b"/>
    </method>
    <method name="ListNames">
      <arg direction="out" type="as"/>
    </method>
    <method name="ListActivatableNames">
      <arg direction="out" type="as"/>
    </method>
    <method name="AddMatch">
      <arg direction="in" type="s"/>
    </method>
    <method name="RemoveMatch">
      <arg direction="in" type="s"/>
    </method>
    <method name="GetNameOwner">
      <arg direction="in" type="s"/>
      <arg direction="out" type="s"/>
    </method>
    <method name="ListQueuedOwners">
      <arg direction="in" type="s"/>
      <arg direction="out" type="as"/>
    </method>
    <method name="GetConnectionUnixUser">
      <arg direction="in" type="s"/>
      <arg direction="out" type="u"/>
    </method>
    <method name="GetConnectionUnixProcessID">
      <arg direction="in" type="s"/>
      <arg direction="out" type="u"/>
    </method>
    <method name="GetAdtAuditSessionData">
      <arg direction="in" type="s"/>
      <arg direction="out" type="ay"/>
    </method>
    <method name="GetConnectionSELinuxSecurityContext">
      <arg direction="in" type="s"/>
      <arg direction="out" type="ay"/>
    </method>
    <method name="ReloadConfig">
    </method>
    <method name="GetId">
      <arg direction="out" type="s"/>
    </method>
    <method name="GetConnectionCredentials">
      <arg direction="in" type="s"/>
      <arg direction="out" type="a{sv}"/>
    </method>
    <property name="Features" type="as" access="read">
      <annotation name="org.freedesktop.DBus.Property.EmitsChangedSignal" value="const"/>
    </property>
    <property name="Interfaces" type="as" access="read">
      <annotation name="org.freedesktop.DBus.Property.EmitsChangedSignal" value="const"/>
    </property>
    <signal name="NameOwnerChanged">
      <arg type="s"/>
      <arg type="s"/>
      <arg type="s"/>
    </signal>
    <signal name="NameLost">
      <arg type="s"/>
    </signal>
    <signal name="NameAcquired">
      <arg type="s"/>
    </signal>
  </interface>
  <interface name="org.freedesktop.DBus.Properties">
    <method name="Get">
      <arg direction="in" type="s"/>
      <arg direction="in" type="s"/>
      <arg direction="out" type="v"/>
    </method>
    <method name="GetAll">
      <arg direction="in" type="s"/>
      <arg direction="out" type="a{sv}"/>
    </method>
    <method name="Set">
      <arg direction="in" type="s"/>
      <arg direction="in" type="s"/>
      <arg direction="in" type="v"/>
    </method>
    <signal name="PropertiesChanged">
      <arg type="s" name="interface_name"/>
      <arg type="a{sv}" name="changed_properties"/>
      <arg type="as" name="invalidated_properties"/>
    </signal>
  </interface>
  <interface name="org.freedesktop.DBus.Introspectable">
    <method name="Introspect">
      <arg direction="out" type="s"/>
    </method>
  </interface>
  <interface name="org.freedesktop.DBus.Monitoring">
    <method name="BecomeMonitor">
      <arg direction="in" type="as"/>
      <arg direction="in" type="u"/>
    </method>
  </interface>
  <interface name="org.freedesktop.DBus.Peer">
    <method name="GetMachineId">
      <arg direction="out" type="s"/>
    </method>
    <method name="Ping">
    </method>
  </interface>
  <interface name="org.freedesktop.DBus.Debug.Stats">
    <method name="GetStats">
      <arg direction="out" type="a{sv}"/>
    </method>
    <method name="GetConnectionStats">
      <arg direction="in" type="s"/>
      <arg direction="out" type="a{sv}"/>
    </method>
    <method name="GetAllMatchRules">
      <arg direction="out" type="a{sas}"/>
    </method>
  </interface>
</node>
)");
  };
}

TEST_F(DBusConnectionTestSuite, TestMethodCall)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    conn = co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, BusType::SESSION);
    auto reply = co_await conn->SendMessage(DBusMessage::Method("NameHasOwner")
                                                .Path(ObjectPath{"/org/freedesktop/DBus"})
                                                .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                                .Destination("org.freedesktop.DBus")
                                                .Parameter(std::string{"com.dbus.CxxTest"}));
    EXPECT_TRUE(reply.GetHeader().GetSignature().has_value());
    EXPECT_EQ(reply.GetHeader().GetSignature().value(), Signature("b"));
    EXPECT_TRUE(reply.HasArguments());
    EXPECT_EQ(reply.Get<bool>(), true);
  };
}

TEST_F(DBusConnectionTestSuite, TestMatchRule)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    conn = co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, BusType::SESSION);

    bool extensiveMatchRuleTriggered{};
    bool simpleMatchRuleTriggered{};
    DBusMatchRule const extensiveRule{DBusMatchRule::Create()
                                          .Type(DBusMessageType::SIGNAL)
                                          .Member("NameOwnerChanged")
                                          .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                          .Sender(DBusWellKnownName{"org.freedesktop.DBus"})};
    co_await conn->AddMatchRule(extensiveRule,
                                [&extensiveMatchRuleTriggered](IncomingDBusMessage) -> boost::asio::awaitable<void>
                                {
                                  extensiveMatchRuleTriggered = true;
                                  co_return;
                                });

    DBusMatchRule const simpleRule{DBusMatchRule::Create().Member("NameOwnerChanged")};
    co_await conn->AddMatchRule(simpleRule,
                                [&simpleMatchRuleTriggered](IncomingDBusMessage) -> boost::asio::awaitable<void>
                                {
                                  simpleMatchRuleTriggered = true;
                                  co_return;
                                });

    co_await conn->SendMessage(DBusMessage::Method("RequestName")
                                   .Path(ObjectPath{"/org/freedesktop/DBus"})
                                   .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                   .Destination("org.freedesktop.DBus")
                                   .Parameter(MultipleCompleteTypes<std::string, uint32_t>{
                                       DBusWellKnownName{"com.dbus.CxxTest2"}, static_cast<uint32_t>(0x1)}));

    EXPECT_TRUE(extensiveMatchRuleTriggered);
    EXPECT_TRUE(simpleMatchRuleTriggered);

    EXPECT_NO_THROW(co_await conn->RemoveMatchRule(extensiveRule));
    EXPECT_NO_THROW(co_await conn->RemoveMatchRule(simpleRule));
  };
}

TEST_F(DBusConnectionTestSuite, TestGettingErrors)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    conn = co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, BusType::SESSION);
    DBusMessage message{DBusMessage::Method("RequestName")
                            .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                            .Path(ObjectPath{"/org/freedesktop/DBus"})
                            .Destination("org.freedesktop.DBus")
                            .Parameter(MultipleCompleteTypes<std::string, uint32_t>{"boo", 0x01})};

    EXPECT_THROW(co_await conn->SendMessage(message), DBusError);

    try
    {
      co_await conn->SendMessage(message);
    }
    catch (DBusError const& ex)
    {
      EXPECT_EQ(ex.GetErrorName(), "org.freedesktop.DBus.Error.InvalidArgs");
      EXPECT_EQ(ex.GetErrorReason(), "The name is not a valid well-known name");
    }
  };
}

TEST_F(DBusConnectionTestSuite, TestReplying)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    LOGGER.LogInfo("Making first connection");
    conn = co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, BusType::SESSION);
    LOGGER.LogInfo("Making second connection");
    auto conn2 = co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest2"}, BusType::SESSION);

    conn2->ReceiveIncomingMessages(
        [conn2](IncomingDBusMessage message) -> boost::asio::awaitable<void>
        {
          // wtf we just got something sent SO stupid. Let's send a reply error back
          LOGGER.LogDebug("Connection2 received the message, returning an error");
          co_await conn2->SendMessageNoReply(DBusMessage::Error(message, "com.you.Stupid", "lol you're so stupid"));
        });

    conn2->RegisterObjectPathHandler(ObjectPath{"/com/dbus/CxxTest2/Method"},
                                     [conn2](IncomingDBusMessage message) -> boost::asio::awaitable<void>
                                     {
                                       LOGGER.LogDebug("Connection2 received the Method call. Returning a reply");
                                       co_await conn2->SendMessageNoReply(DBusMessage::Reply(message).Parameter(
                                           MultipleCompleteTypes<std::string, uint32_t>{"Hello from connection2", 42}));
                                     });

    LOGGER.LogDebug("Sending a message from connection1 to connection2");
    EXPECT_THROW(
        co_await conn->SendMessage(
            DBusMessage::Method("Wow").Destination("com.dbus.CxxTest2").Path(ObjectPath{"/com/dbus/CxxTest2"})),
        DBusError);
    try
    {
      co_await conn->SendMessage(
          DBusMessage::Method("Wow").Destination("com.dbus.CxxTest2").Path(ObjectPath{"/com/dbus/CxxTest2"}));
    }
    catch (DBusError const& ex)
    {
      EXPECT_EQ(ex.GetErrorName(), "com.you.Stupid");
      EXPECT_EQ(ex.GetErrorReason(), "lol you're so stupid");
    }

    EXPECT_EQ(((co_await conn->SendMessage(DBusMessage::Method("Method")
                                               .Path(ObjectPath{"/com/dbus/CxxTest2/Method"})
                                               .Destination("com.dbus.CxxTest2")))
                   .Get<MultipleCompleteTypes<std::string, uint32_t>>()),
              (MultipleCompleteTypes<std::string, uint32_t>{"Hello from connection2", 42}));

    co_await conn2->Close();
    LOGGER.LogTrace("Finished closing 2nd connection");
  };
}

TEST_F(DBusConnectionTestSuite, TestEmittingSignal)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    conn = co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, BusType::SESSION);
    auto conn2 = co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest2"}, BusType::SESSION);

    std::shared_ptr<boost::asio::experimental::channel<void(boost::system::error_code)>> chann{
        std::make_shared<boost::asio::experimental::channel<void(boost::system::error_code)>>(ioService, 1)};
    bool signalEmitted{};
    co_await conn2->AddMatchRule(
        DBusMatchRule::Create().Member("SignalEmitted"),
        [&signalEmitted, chann, this](IncomingDBusMessage msg) -> boost::asio::awaitable<void>
        {
          LOGGER.LogInfo("Received emitted signal");
          signalEmitted = true;
          EXPECT_EQ((msg.Get<std::tuple<std::string, int, double, std::string>>()),
                    (std::tuple<std::string, int, double, std::string>{"Hello", 456, 3.1415, "World!"}));

          boost::asio::co_spawn(
              ioService, [chann]() -> boost::asio::awaitable<void>
              { co_await chann->async_send(boost::system::error_code{}); }, boost::asio::detached);
          co_return;
        });

    co_await conn->SendMessageNoReply(
        DBusMessage::Signal("SignalEmitted")
            .Interface(DBusInterfaceName{"com.dbus.CxxTest"})
            .Path(ObjectPath{"/com/dbus/CxxTest"})
            .Parameter(std::tuple<std::string, int, double, std::string>{"Hello", 456, 3.1415, "World!"}));

    LOGGER.LogDebug("Waiting for signal to be received");
    co_await chann->async_receive(boost::asio::use_awaitable);
    EXPECT_TRUE(signalEmitted);

    co_await conn2->Close();
  };
}

// This test is expected to fail if the system bus is not available or if the user does not have permission to access it
TEST_F(DBusConnectionTestSuite, TestSystemBus)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    try
    {
      conn = co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, BusType::SYSTEM);
      auto reply = co_await conn->SendMessage(DBusMessage::Method("NameHasOwner")
                                                  .Path(ObjectPath{"/org/freedesktop/DBus"})
                                                  .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                                  .Destination("org.freedesktop.DBus")
                                                  .Parameter(std::string{"com.dbus.CxxTest"}));
      EXPECT_TRUE(reply.GetHeader().GetSignature().has_value());
      EXPECT_EQ(reply.GetHeader().GetSignature().value(), Signature("b"));
      EXPECT_TRUE(reply.HasArguments());
      EXPECT_EQ(reply.Get<bool>(), true);
    }
    catch (DBusError const& ex)
    {
      if (ex.GetErrorName() == "org.freedesktop.DBus.Error.AccessDenied")
      {
        LOGGER.LogInfo("Access denied to system bus. Test skipped.");
        co_return;
      }
      else
      {
        LOGGER.LogError(std::format("DBusError: {} - {}", ex.GetErrorName(), ex.GetErrorReason()));
        throw;
      }
    }
  };
}

TEST_F(DBusConnectionTestSuite, TestMessageFilter)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    conn = co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, BusType::SESSION);
    auto conn2 = co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest2"}, BusType::SESSION);

    int nrOfCalls{};
    uint32_t const id = conn2->RegisterMessageFilter(
        [&nrOfCalls, conn2](IncomingDBusMessage msg) -> boost::asio::awaitable<MessageHandled>
        {
          LOGGER.LogInfo(std::format("Message filter called for message with member '{}'",
                                     msg.GetHeader().GetMember().value_or("")));
          ++nrOfCalls;

          if (msg.GetHeader().GetMember() == "Handle")
          {
            co_await conn2->SendMessageNoReply(DBusMessage::Reply(msg));
            co_return MessageHandled::YES;
          }

          co_return MessageHandled::NO;
        });

    std::shared_ptr<boost::asio::experimental::channel<void(boost::system::error_code)>> chann{
        std::make_shared<boost::asio::experimental::channel<void(boost::system::error_code)>>(ioService, 2)};

    bool objectPathHandlerCalled{};
    conn2->RegisterObjectPathHandler(
        ObjectPath{"/com/dbus/CxxTest2/Foo"},
        [chann, &objectPathHandlerCalled, conn2](IncomingDBusMessage msg) -> boost::asio::awaitable<void>
        {
          LOGGER.LogInfo("Object path handler called");
          objectPathHandlerCalled = !objectPathHandlerCalled;
          co_await conn2->SendMessageNoReply(DBusMessage::Reply(msg));
          co_return co_await chann->async_send(boost::system::error_code{});
        });

    co_await conn->SendMessage(
        DBusMessage::Method("Handle").Destination("com.dbus.CxxTest2").Path(ObjectPath{"/com/dbus/CxxTest2/Foo"}));
    co_await conn->SendMessage(
        DBusMessage::Method("DoNotHandle").Destination("com.dbus.CxxTest2").Path(ObjectPath{"/com/dbus/CxxTest2/Foo"}));

    co_await chann->async_receive(boost::asio::use_awaitable);

    // If 'RegisterObjectPathHandler' gets called not exactly 1 time, then 'objectPathHandlerCalled' will be false
    EXPECT_TRUE(objectPathHandlerCalled);
    EXPECT_EQ(nrOfCalls, 2);

    conn2->UnregisterMessageFilter(id);
    co_await conn->SendMessage(
        DBusMessage::Method("Handle").Destination("com.dbus.CxxTest2").Path(ObjectPath{"/com/dbus/CxxTest2/Foo"}));

    co_await chann->async_receive(boost::asio::use_awaitable);

    // It got called a 3rd time, making it false again
    EXPECT_FALSE(objectPathHandlerCalled);
    EXPECT_EQ(nrOfCalls, 2);  // should not have been called

    co_await conn2->Close();
  };
}

TEST_F(DBusConnectionTestSuite, TestCallingUnknownMethod)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    conn = co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, BusType::SESSION);

    EXPECT_THROW(
        co_await conn->SendMessage(
            DBusMessage::Method("UnknownMethod").Destination("com.dbus.CxxTest").Path(ObjectPath{"/com/dbus/CxxTest"})),
        DBusError);
  };
}

TEST_F(DBusConnectionTestSuite, TestMixSyncAndAsync)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    std::shared_ptr<boost::asio::experimental::channel<void(boost::system::error_code)>> chann{
        std::make_shared<boost::asio::experimental::channel<void(boost::system::error_code)>>(ioService, 1)};
    conn = DBusConnection::CreateDetached(
        ioService, DBusWellKnownName{"com.dbus.CxxTest"},
        [this, chann]() -> boost::asio::awaitable<void>
        {
          boost::asio::co_spawn(
              ioService, [chann]() -> boost::asio::awaitable<void>
              { co_await chann->async_send(boost::system::error_code{}); }, boost::asio::detached);
          co_return;
        },
        BusType::SESSION);

    co_await chann->async_receive(boost::asio::use_awaitable);

    // conn->RequestWellKnownNameSync(DBusWellKnownName{"com.dbus.CxxTest2"});
    // co_await conn->RequestWellKnownName(DBusWellKnownName{"com.dbus.CxxTest3"});
  };
}
