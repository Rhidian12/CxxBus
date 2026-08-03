#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/experimental/basic_channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/detail/error_code.hpp>
#include <functional>
#include <memory>

#include "src/DBusMatchRule.h"
#include "src/DBusMessage.h"
#include "src/DBusTypes.h"
#include "src/IncomingDBusMessage.h"
#include "src/Log.h"
#include "src/SyncDBusConnection.h"

using namespace cxxbus;

Logger const LOGGER{.logLevel = LogLevel::Trace};

struct SyncDBusConnectionTestSuite : ::testing::Test
{
 public:
  boost::asio::io_context ioService;
  std::shared_ptr<SyncDBusConnection> conn;
  std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> workGuard;
  std::thread ioThread;

  void StartIoThread()
  {
    workGuard.emplace(boost::asio::make_work_guard(ioService));
    ioThread = std::thread([this] { ioService.run(); });
  }

  void TearDown() override
  {
    if (ioThread.joinable())
    {
      workGuard.reset();
      ioService.stop();
      ioThread.join();
    }
  }
};

TEST_F(SyncDBusConnectionTestSuite, TestConnectingToDBusDaemon)
{
  conn = SyncDBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"});
  EXPECT_TRUE(conn != nullptr);
}

TEST_F(SyncDBusConnectionTestSuite, TestIntrospectingDBusDaemon)
{
  conn = SyncDBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"});
  auto reply = conn->SendMessage(DBusMessage::Method("Introspect")
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
}

TEST_F(SyncDBusConnectionTestSuite, TestMethodCall)
{
  conn = SyncDBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"});
  auto reply = conn->SendMessage(DBusMessage::Method("NameHasOwner")
                                     .Path(ObjectPath{"/org/freedesktop/DBus"})
                                     .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                     .Destination("org.freedesktop.DBus")
                                     .Parameter(std::string{"com.dbus.CxxTest"}));
  EXPECT_TRUE(reply.GetHeader().GetSignature().has_value());
  EXPECT_EQ(reply.GetHeader().GetSignature().value(), Signature("b"));
  EXPECT_TRUE(reply.HasArguments());
  EXPECT_EQ(reply.Get<bool>(), true);
}

TEST_F(SyncDBusConnectionTestSuite, TestMatchRule)
{
  conn = SyncDBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"});

  auto dispatchMessages = [this]()
  {
    while (conn->DispatchIncomingMessages() != DispatchStatus::COMPLETED)
    {
      // boo
    }
  };

  conn->SetDispatchHandler(
      [dispatchMessages](DispatchStatus status)
      {
        if (status == DispatchStatus::DISPATCH_PENDING)
        {
          dispatchMessages();
        }
      });

  bool extensiveMatchRuleTriggered{};
  bool simpleMatchRuleTriggered{};

  DBusMatchRule const extensiveRule{DBusMatchRule::Create()
                                        .Type(DBusMessageType::SIGNAL)
                                        .Member("NameOwnerChanged")
                                        .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                        .Sender(DBusWellKnownName{"org.freedesktop.DBus"})};
  conn->AddMatchRule(extensiveRule,
                     [&extensiveMatchRuleTriggered](IncomingDBusMessage) { extensiveMatchRuleTriggered = true; });

  DBusMatchRule const simpleRule{DBusMatchRule::Create().Member("NameOwnerChanged")};
  conn->AddMatchRule(simpleRule, [&simpleMatchRuleTriggered](IncomingDBusMessage) { simpleMatchRuleTriggered = true; });

  conn->SendMessage(DBusMessage::Method("RequestName")
                        .Path(ObjectPath{"/org/freedesktop/DBus"})
                        .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                        .Destination("org.freedesktop.DBus")
                        .Parameter(MultipleCompleteTypes<std::string, uint32_t>{DBusWellKnownName{"com.dbus.CxxTest2"},
                                                                                static_cast<uint32_t>(0x1)}));

  EXPECT_TRUE(extensiveMatchRuleTriggered);
  EXPECT_TRUE(simpleMatchRuleTriggered);

  EXPECT_NO_THROW(conn->RemoveMatchRule(extensiveRule));
  EXPECT_NO_THROW(conn->RemoveMatchRule(simpleRule));
}

TEST_F(SyncDBusConnectionTestSuite, TestGettingErrors)
{
  conn = SyncDBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"});
  DBusMessage message{DBusMessage::Method("RequestName")
                          .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                          .Path(ObjectPath{"/org/freedesktop/DBus"})
                          .Destination("org.freedesktop.DBus")
                          .Parameter(MultipleCompleteTypes<std::string, uint32_t>{"boo", 0x01})};

  EXPECT_THROW(conn->SendMessage(message), DBusError);

  try
  {
    conn->SendMessage(message);
  }
  catch (DBusError const& ex)
  {
    EXPECT_EQ(ex.GetErrorName(), "org.freedesktop.DBus.Error.InvalidArgs");
    EXPECT_EQ(ex.GetErrorReason(), "The name is not a valid well-known name");
  }
}

boost::asio::awaitable<void> PollSocket(boost::asio::io_context& ioContext, std::weak_ptr<SyncDBusConnection> conn,
                                        boost::asio::local::stream_protocol::socket& socket);
void PollConnection(boost::asio::io_context& ioContext, std::weak_ptr<SyncDBusConnection> conn,
                    boost::asio::local::stream_protocol::socket& socket)
{
  if (conn.expired()) return;

  conn.lock()->Poll();
  boost::asio::co_spawn(ioContext, PollSocket(ioContext, conn, socket), boost::asio::detached);
}

boost::asio::awaitable<void> PollSocket(boost::asio::io_context& ioContext, std::weak_ptr<SyncDBusConnection> conn,
                                        boost::asio::local::stream_protocol::socket& socket)
{
  socket.async_wait(boost::asio::local::stream_protocol::socket::wait_read,
                    [&](boost::system::error_code const&) { PollConnection(ioContext, conn, socket); });
  co_return;
}

TEST_F(SyncDBusConnectionTestSuite, TestReplying)
{
  LOGGER.LogInfo("Making first connection");
  conn = SyncDBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"});
  LOGGER.LogInfo("Making second connection");
  auto conn2 = SyncDBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest2"});

  auto dispatchMessages = [](std::weak_ptr<SyncDBusConnection> conn)
  {
    if (conn.expired()) return;
    while (conn.lock()->DispatchIncomingMessages() != DispatchStatus::COMPLETED)
    {
      // boo
    }
  };

  auto weakConn = std::weak_ptr{conn};
  conn->SetDispatchHandler(
      [dispatchMessages, weakConn](DispatchStatus status)
      {
        if (status == DispatchStatus::DISPATCH_PENDING)
        {
          dispatchMessages(weakConn);
        }
      });

  conn2->SetDispatchHandler(
      [dispatchMessages, conn = std::weak_ptr{conn2}](DispatchStatus status)
      {
        if (conn.expired()) return;
        if (status == DispatchStatus::DISPATCH_PENDING)
        {
          dispatchMessages(conn);
        }
      });

  StartIoThread();

  boost::asio::io_context& ioContext{ioService};
  conn2->SetPollHandler(
      [&ioContext, weakConn = std::weak_ptr{conn2}](boost::asio::local::stream_protocol::socket& socket)
      { boost::asio::co_spawn(ioContext, PollSocket(ioContext, weakConn, socket), boost::asio::detached); });

  conn2->ReceiveIncomingMessages(
      [conn = std::weak_ptr{conn2}](IncomingDBusMessage message)
      {
        if (conn.expired()) return;
        // wtf we just got something sent SO stupid. Let's send a reply error back
        LOGGER.LogDebug("Connection2 received the message, returning an error");
        conn.lock()->SendMessageNoReply(DBusMessage::Error(message, "com.you.Stupid", "lol you're so stupid"));
      });

  conn2->RegisterObjectPathHandler(ObjectPath{"/com/dbus/CxxTest2/Method"},
                                   [conn = std::weak_ptr{conn2}](IncomingDBusMessage message)
                                   {
                                     if (conn.expired()) return;
                                     LOGGER.LogDebug("Connection2 received the Method call. Returning a reply");
                                     conn.lock()->SendMessageNoReply(DBusMessage::Reply(message).Parameter(
                                         MultipleCompleteTypes<std::string, uint32_t>{"Hello from connection2", 42}));
                                   });

  LOGGER.LogDebug("Sending a message from connection1 to connection2");
  EXPECT_THROW(conn->SendMessage(
                   DBusMessage::Method("Wow").Destination("com.dbus.CxxTest2").Path(ObjectPath{"/com/dbus/CxxTest2"})),
               DBusError);
  try
  {
    conn->SendMessage(
        DBusMessage::Method("Wow").Destination("com.dbus.CxxTest2").Path(ObjectPath{"/com/dbus/CxxTest2"}));
  }
  catch (DBusError const& ex)
  {
    EXPECT_EQ(ex.GetErrorName(), "com.you.Stupid");
    EXPECT_EQ(ex.GetErrorReason(), "lol you're so stupid");
  }

  EXPECT_EQ(((conn->SendMessage(DBusMessage::Method("Method")
                                    .Path(ObjectPath{"/com/dbus/CxxTest2/Method"})
                                    .Destination("com.dbus.CxxTest2")))
                 .Get<MultipleCompleteTypes<std::string, uint32_t>>()),
            (MultipleCompleteTypes<std::string, uint32_t>{"Hello from connection2", 42}));

  LOGGER.LogTrace("Finished closing 2nd connection");
}

TEST_F(SyncDBusConnectionTestSuite, TestEmittingSignal)
{
  conn = SyncDBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"});
  conn->SetLogLevel(LogLevel::Error);
  auto conn2 = SyncDBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest2"});

  std::shared_ptr<bool> signalEmitted{std::make_shared<bool>()};
  conn2->AddMatchRule(DBusMatchRule::Create().Member("SignalEmitted"),
                      [signalEmitted](IncomingDBusMessage msg)
                      {
                        LOGGER.LogInfo("Received emitted signal");
                        *signalEmitted = true;
                        EXPECT_EQ((msg.Get<std::tuple<std::string, int, double, std::string>>()),
                                  (std::tuple<std::string, int, double, std::string>{"Hello", 456, 3.1415, "World!"}));
                      });

  auto dispatchMessages = [](std::weak_ptr<SyncDBusConnection> conn)
  {
    if (conn.expired()) return;
    while (conn.lock()->DispatchIncomingMessages() != DispatchStatus::COMPLETED)
    {
      // boo
    }
  };

  conn2->SetDispatchHandler(
      [dispatchMessages, conn = std::weak_ptr{conn2}](DispatchStatus status)
      {
        if (conn.expired()) return;
        if (status == DispatchStatus::DISPATCH_PENDING)
        {
          dispatchMessages(conn);
        }
      });

  conn->SendMessageNoReply(
      DBusMessage::Signal("SignalEmitted")
          .Interface(DBusInterfaceName{"com.dbus.CxxTest"})
          .Path(ObjectPath{"/com/dbus/CxxTest"})
          .Parameter(std::tuple<std::string, int, double, std::string>{"Hello", 456, 3.1415, "World!"}));

  // Send a message to the bus, this will pump our queue of messages
  conn2->SendMessage(DBusMessage::Method("NameHasOwner")
                         .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                         .Destination("org.freedesktop.DBus")
                         .Path(ObjectPath{"/org/freedesktop/DBus"})
                         .Parameter(std::string{"com.dbus.CxxTest2"}));

  EXPECT_TRUE(*signalEmitted);
}