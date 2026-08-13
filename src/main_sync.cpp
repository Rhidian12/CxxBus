#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <cstdint>
#include <memory>

#include "DBusConnection.h"
#include "DBusMatchRule.h"
#include "DBusMessage.h"
#include "DBusTypes.h"
#include "IncomingDBusMessage.h"
#include "Log.h"

using namespace cxxbus;

namespace
{
  Logger const LOGGER{.logLevel = LogLevel::INFO};
}

void DBusSubscribeToSignal(std::shared_ptr<DBusConnection> conn)
{
  conn->AddMatchRuleSync(DBusMatchRule::Create()
                             .Type(DBusMessageType::SIGNAL)
                             .Member("NameOwnerChanged")
                             .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                             .Sender(DBusWellKnownName{"org.freedesktop.DBus"}),
                         [](IncomingDBusMessage message)
                         {
                           LOGGER.LogInfo(
                               "Received NameOwnerChanged signal. New Name: {}, Sender: {}",
                               message.Get<MultipleCompleteTypes<std::string, std::string, std::string>>().GetType<2>(),
                               message.GetHeader().GetSender().value_or(""));
                         });

  IncomingDBusMessage reply =
      conn->SendMessageSync(DBusMessage::Method("RequestName")
                                .Path(ObjectPath{"/org/freedesktop/DBus"})
                                .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                .Destination("org.freedesktop.DBus")
                                .Parameter(MultipleCompleteTypes<std::string, uint32_t>{
                                    DBusWellKnownName{"com.dbus.CxxTest2"}, static_cast<uint32_t>(0x1)}));
  LOGGER.LogInfo(std::format("Reply to first name change: {}", reply.Get<uint32_t>()));
  reply = conn->SendMessageSync(
      DBusMessage::Method("RequestName")
          .Path(ObjectPath{"/org/freedesktop/DBus"})
          .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
          .Destination("org.freedesktop.DBus")
          .Parameter(MultipleCompleteTypes<std::string, uint32_t>{"com.dbus.CxxTest3", static_cast<uint32_t>(0x1)}));
  LOGGER.LogInfo("Reply to second name change: {}", reply.Get<uint32_t>());
}

int main()
{
  boost::asio::io_context ioService{};

  boost::asio::co_spawn(
      ioService,
      [&ioService]() -> boost::asio::awaitable<void>
      {
        LOGGER.LogInfo("Running Sync Main");
        std::shared_ptr<DBusConnection> conn{
            DBusConnection::CreateSync(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, BusType::SESSION)};

        DBusSubscribeToSignal(conn);

        co_return;
      },
      boost::asio::detached);

  LOGGER.LogInfo("Running IOService");
  ioService.run();
}
