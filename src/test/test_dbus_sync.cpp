#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/basic_channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/detail/error_code.hpp>
#include <functional>
#include <memory>

#include "src/DBusConnection.h"
#include "src/DBusMatchRule.h"
#include "src/DBusMessage.h"
#include "src/DBusTypes.h"
#include "src/IncomingDBusMessage.h"
#include "src/Log.h"

using namespace cxxbus;

Logger const LOGGER{.logLevel = LogLevel::Trace};

struct SyncDBusConnectionTestSuite : ::testing::Test
{
 public:
  boost::asio::io_context ioService;
  std::function<boost::asio::awaitable<void>()> coroutineToRun;

  void TearDown() override
  {
    EXPECT_NO_THROW(boost::asio::co_spawn(
        ioService,
        [this]() -> boost::asio::awaitable<void>
        {
          co_await coroutineToRun();
          LOGGER.LogTrace("Finished running coroutine");
        },
        [](std::exception_ptr e)
        {
          if (e) std::rethrow_exception(e);
        }));

    ioService.run();
  }
};

TEST_F(SyncDBusConnectionTestSuite, TestIntrospectingDBusDaemon)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    auto conn = DBusConnection::CreateSync(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, BusType::SESSION);
    auto reply = conn->SendMessageSync(DBusMessage::Method("Introspect")
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

    co_return;
  };
}

TEST_F(SyncDBusConnectionTestSuite, TestMethodCall)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    auto conn = DBusConnection::CreateSync(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, BusType::SESSION);
    auto reply = conn->SendMessageSync(DBusMessage::Method("NameHasOwner")
                                           .Path(ObjectPath{"/org/freedesktop/DBus"})
                                           .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                           .Destination("org.freedesktop.DBus")
                                           .Parameter(std::string{"com.dbus.CxxTest"}));
    EXPECT_TRUE(reply.GetHeader().GetSignature().has_value());
    EXPECT_EQ(reply.GetHeader().GetSignature().value(), Signature("b"));
    EXPECT_TRUE(reply.HasArguments());
    EXPECT_EQ(reply.Get<bool>(), true);

    co_return;
  };
}

TEST_F(SyncDBusConnectionTestSuite, TestMatchRule)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    auto conn = DBusConnection::CreateSync(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, BusType::SESSION);

    bool extensiveMatchRuleTriggered{};
    bool simpleMatchRuleTriggered{};
    std::shared_ptr<boost::asio::experimental::channel<void(boost::system::error_code)>> chann{
        std::make_shared<boost::asio::experimental::channel<void(boost::system::error_code)>>(ioService, 1)};
    std::shared_ptr<boost::asio::experimental::channel<void(boost::system::error_code)>> chann2{
        std::make_shared<boost::asio::experimental::channel<void(boost::system::error_code)>>(ioService, 1)};

    LOGGER.LogDebug("Adding extensive match rule");
    DBusMatchRule const extensiveRule{DBusMatchRule::Create()
                                          .Type(DBusMessageType::SIGNAL)
                                          .Member("NameOwnerChanged")
                                          .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                          .Sender(DBusWellKnownName{"org.freedesktop.DBus"})};
    conn->AddMatchRuleSync(extensiveRule,
                           [&extensiveMatchRuleTriggered, chann](IncomingDBusMessage)
                           {
                             LOGGER.LogInfo("Extensive match rule was triggered");
                             extensiveMatchRuleTriggered = true;
                             chann->async_send(boost::system::error_code{}, boost::asio::detached);
                           });

    LOGGER.LogDebug("Adding simple match rule");
    DBusMatchRule const simpleRule{DBusMatchRule::Create().Member("NameOwnerChanged")};
    conn->AddMatchRuleSync(simpleRule,
                           [&simpleMatchRuleTriggered, chann2](IncomingDBusMessage)
                           {
                             LOGGER.LogInfo("Simple match rule was triggered");
                             simpleMatchRuleTriggered = true;
                             chann2->async_send(boost::system::error_code{}, boost::asio::detached);
                           });

    conn->SendMessageSync(DBusMessage::Method("RequestName")
                              .Path(ObjectPath{"/org/freedesktop/DBus"})
                              .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                              .Destination("org.freedesktop.DBus")
                              .Parameter(MultipleCompleteTypes<std::string, uint32_t>{
                                  DBusWellKnownName{"com.dbus.CxxTest2"}, static_cast<uint32_t>(0x1)}));
    LOGGER.LogInfo("Finished request name call");

    co_await chann->async_receive(boost::asio::use_awaitable);
    co_await chann2->async_receive(boost::asio::use_awaitable);

    EXPECT_TRUE(extensiveMatchRuleTriggered);
    EXPECT_TRUE(simpleMatchRuleTriggered);

    EXPECT_NO_THROW(conn->RemoveMatchRuleSync(extensiveRule));
    EXPECT_NO_THROW(conn->RemoveMatchRuleSync(simpleRule));

    co_return;
  };
}

TEST_F(SyncDBusConnectionTestSuite, TestGettingErrors)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    auto conn = DBusConnection::CreateSync(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, BusType::SESSION);
    DBusMessage message{DBusMessage::Method("RequestName")
                            .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                            .Path(ObjectPath{"/org/freedesktop/DBus"})
                            .Destination("org.freedesktop.DBus")
                            .Parameter(MultipleCompleteTypes<std::string, uint32_t>{"boo", 0x01})};

    EXPECT_THROW(conn->SendMessageSync(message), DBusError);

    try
    {
      conn->SendMessageSync(message);
    }
    catch (DBusError const& ex)
    {
      EXPECT_EQ(ex.GetErrorName(), "org.freedesktop.DBus.Error.InvalidArgs");
#ifdef GITHUB_ACTIONS
      EXPECT_EQ(ex.GetErrorReason(), "Requested bus name \"boo\" is not valid");
#else
      EXPECT_EQ(ex.GetErrorReason(), "The name is not a valid well-known name");
#endif
    }
    co_return;
  };
}

TEST_F(SyncDBusConnectionTestSuite, TestEmittingSignal)
{
  coroutineToRun = [this]() -> boost::asio::awaitable<void>
  {
    auto conn = DBusConnection::CreateSync(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, BusType::SESSION);
    auto conn2 = DBusConnection::CreateSync(ioService, DBusWellKnownName{"com.dbus.CxxTest2"}, BusType::SESSION);

    std::shared_ptr<bool> signalEmitted{std::make_shared<bool>()};
    std::shared_ptr<boost::asio::experimental::channel<void(boost::system::error_code)>> chann{
        std::make_shared<boost::asio::experimental::channel<void(boost::system::error_code)>>(ioService, 1)};

    conn2->AddMatchRuleSync(DBusMatchRule::Create().Member("SignalEmitted"),
                            [signalEmitted, chann](IncomingDBusMessage msg)
                            {
                              LOGGER.LogInfo("Received emitted signal");
                              *signalEmitted = true;
                              EXPECT_EQ(
                                  (msg.Get<std::tuple<std::string, int, double, std::string>>()),
                                  (std::tuple<std::string, int, double, std::string>{"Hello", 456, 3.1415, "World!"}));
                              chann->async_send(boost::system::error_code{}, boost::asio::detached);
                            });

    conn->SendMessageNoReplySync(
        DBusMessage::Signal("SignalEmitted")
            .Interface(DBusInterfaceName{"com.dbus.CxxTest"})
            .Path(ObjectPath{"/com/dbus/CxxTest"})
            .Parameter(std::tuple<std::string, int, double, std::string>{"Hello", 456, 3.1415, "World!"}));

    co_await chann->async_receive(boost::asio::use_awaitable);

    EXPECT_TRUE(*signalEmitted);
  };
}
