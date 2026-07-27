#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <functional>
#include <iostream>

#include "src/DBusConnection.h"
#include "src/DBusMessage.h"

struct DBusConnectionTestSuite : ::testing::Test
{
 public:
  boost::asio::io_context ioService;
  std::function<boost::asio::awaitable<void>()> coroutineToRun;

  void SetUp() override
  {
    // Setup code here.
  }

  void TearDown() override
  {
    EXPECT_NO_THROW(boost::asio::co_spawn(ioService, coroutineToRun(),
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
    auto conn = co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, CreateConnectionDetached::NO);
    EXPECT_TRUE(conn != nullptr);
  };
}

TEST_F(DBusConnectionTestSuite, TestIntrospectingDBusDaemon)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    auto conn = co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, CreateConnectionDetached::NO);
    auto reply = co_await conn->SendMessage(
        DBusMessage::Create("Introspect").Path(ObjectPath{"/org/freedesktop/DBus"}).Interface("org.freedesktop.DBus.Introspectable").Destination("org.freedesktop.DBus"));
  
    EXPECT_TRUE(reply.has_value());
    EXPECT_TRUE(reply.value().GetHeader().GetSignature().has_value());
    EXPECT_EQ(reply.value().GetHeader().GetSignature().value(), Signature("s"));
    EXPECT_TRUE(reply.value().HasArguments());
    EXPECT_EQ(reply.value().Get<std::string>(), R"(<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN"
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
  coroutineToRun = [this]()->boost::asio::awaitable<void>
  {
    auto conn = co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, CreateConnectionDetached::NO);
    auto reply = co_await conn->SendMessage(DBusMessage::Create("NameHasOwner").Path(ObjectPath{"/org/freedesktop/DBus"}).Interface("org.freedesktop.DBus").Destination("org.freedesktop.DBus").Parameter(std::string{"com.dbus.CxxTest"}));
    EXPECT_TRUE(reply.has_value());
    EXPECT_TRUE(reply.value().GetHeader().GetSignature().has_value());
    EXPECT_EQ(reply.value().GetHeader().GetSignature().value(), Signature("b"));
    EXPECT_TRUE(reply.value().HasArguments());
    EXPECT_EQ(reply.value().Get<bool>(), true);
  };
}