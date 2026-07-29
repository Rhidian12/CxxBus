#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <cstdint>
#include <exception>
#include <memory>

#include "DBusConnection.h"
#include "DBusMessage.h"
#include "DBusTypes.h"
#include "IncomingDBusMessage.h"
#include "DBusMatchRule.h"
#include "Log.h"

namespace
{
  Logger const LOGGER{.logLevel = LogLevel::Info};
}

boost::asio::awaitable<void> DBusEchoTest(std::shared_ptr<DBusConnection> conn)
{
  LOGGER.LogInfo("Sending first message");
  co_await conn->SendMessage(DBusMessage::Method("EchoMethod").Path(ObjectPath{"/echo"}).Interface("com.example.Echo").Destination("com.example.Echo"));
  LOGGER.LogInfo("Sending second message");
  co_await conn->SendMessage(DBusMessage::Method("EchoMethod").Path(ObjectPath{"/echo"}).Interface("com.example.Echo").Destination("com.example.Echo"));
  LOGGER.LogInfo("Sending third message");
  co_await conn->SendMessage(DBusMessage::Method("EchoMethod").Path(ObjectPath{"/echo"}).Interface("com.example.Echo").Destination("com.example.Echo"));
  LOGGER.LogInfo("Sending fourth message");
  co_await conn->SendMessage(DBusMessage::Method("EchoMethod").Path(ObjectPath{"/echo"}).Interface("com.example.Echo").Destination("com.example.Echo"));
}

boost::asio::awaitable<void> DBusReceiveMessagesTest(std::shared_ptr<DBusConnection> conn, boost::asio::io_context& ioService)
{
  conn->ReceiveIncomingMessages(
      [](IncomingDBusMessage message)
      {
        DBusMessageHeader const& header = message.GetHeader();
        LOGGER.LogInfo(std::format("Message Received! Member: {}, Sender: {}, Destination: {}, Interface: {}, Message Type: {}",
                                   header.GetMember().value_or(""), header.GetSender().value_or(""), header.GetDestination().value_or(""),
                                   header.GetInterface().value_or(""), static_cast<int>(header.GetMessageType())));
      });

  boost::asio::system_timer timer{ioService};
  timer.expires_after(std::chrono::seconds(5));
  co_await timer.async_wait(boost::asio::use_awaitable);

  co_return;
}

boost::asio::awaitable<void> DBusSubscribeToSignal(std::shared_ptr<DBusConnection> conn, boost::asio::io_context& ioService)
{
  co_await conn->AddMatchRule(DBusMatchRule::Create()
                                  .Type(DBusMessageType::SIGNAL)
                                  .Member("NameOwnerChanged")
                                  .Interface("org.freedesktop.DBus")
                                  .Sender(DBusWellKnownName{"org.freedesktop.DBus"}),
                              [](IncomingDBusMessage message)
                              {
                                LOGGER.LogInfo(std::format("Received NameOwnerChanged signal. New Name: {}, Sender: {}",
                                                           message.Get<MultipleCompleteTypes<std::string, std::string, std::string>>().GetType<2>(),
                                                           message.GetHeader().GetSender().value_or("")));
                              });

  IncomingDBusMessage reply = co_await conn->SendMessage(
      DBusMessage::Method("RequestName")
          .Path(ObjectPath{"/org/freedesktop/DBus"})
          .Interface("org.freedesktop.DBus")
          .Destination("org.freedesktop.DBus")
          .Parameter(MultipleCompleteTypes<std::string, uint32_t>{DBusWellKnownName{"com.dbus.CxxTest2"}, static_cast<uint32_t>(0x1)}));
  LOGGER.LogInfo(std::format("Reply to first name change: {}", reply.Get<uint32_t>()));
  reply = co_await conn->SendMessage(DBusMessage::Method("RequestName")
                                         .Path(ObjectPath{"/org/freedesktop/DBus"})
                                         .Interface("org.freedesktop.DBus")
                                         .Destination("org.freedesktop.DBus")
                                         .Parameter(MultipleCompleteTypes<std::string, uint32_t>{"com.dbus.CxxTest3", static_cast<uint32_t>(0x1)}));
  LOGGER.LogInfo(std::format("Reply to second name change: {}", reply.Get<uint32_t>()));

  boost::asio::system_timer timer{ioService};
  timer.expires_after(std::chrono::seconds(5));
  co_await timer.async_wait(boost::asio::use_awaitable);
}

boost::asio::awaitable<void> DBusGetErrorReply(std::shared_ptr<DBusConnection> conn)
{
  LOGGER.LogInfo("Sending first message");
  try
  {
    co_await conn->SendMessage(DBusMessage::Method("RequestName")
                                   .Interface("org.freedesktop.DBus")
                                   .Path(ObjectPath{"/org/freedesktop/DBus"})
                                   .Destination("org.freedesktop.DBus")
                                   .Parameter(MultipleCompleteTypes<std::string, uint32_t>{"boo", 0x01}));
  }
  catch (std::exception const & ex)
  {
    LOGGER.LogError(ex.what());
  }
}

boost::asio::awaitable<void> AsyncMain(boost::asio::io_context& ioService)
{
  std::shared_ptr<DBusConnection> conn{co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, CreateConnectionDetached::YES)};

  // co_await DBusEchoTest(conn);
  // co_await DBusReceiveMessagesTest(conn, ioService);
  // co_await DBusSubscribeToSignal(conn, ioService);
  co_await DBusGetErrorReply(conn);

  co_await conn->Close();
}

int main()
{
  boost::asio::io_context ioService;

  boost::asio::co_spawn(ioService, AsyncMain(ioService),
                        [](std::exception_ptr e)
                        {
                          if (e) std::rethrow_exception(e);
                        });

  ioService.run();
}
