#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <exception>
#include <iostream>
#include <memory>

#include "DBusConnection.h"
#include "DBusMessage.h"
#include "DBusTypes.h"
#include "IncomingDBusMessage.h"

boost::asio::awaitable<void> DBusEchoTest(std::shared_ptr<DBusConnection> conn)
{
  std::cout << "Sending first message\n";
  co_await conn->SendMessage(DBusMessage::Create("EchoMethod").Path(ObjectPath{"/echo"}).Interface("com.example.Echo").Destination("com.example.Echo"));
  std::cout << "Sending second message\n";
  co_await conn->SendMessage(DBusMessage::Create("EchoMethod").Path(ObjectPath{"/echo"}).Interface("com.example.Echo").Destination("com.example.Echo"));
  std::cout << "Sending third message\n";
  co_await conn->SendMessage(DBusMessage::Create("EchoMethod").Path(ObjectPath{"/echo"}).Interface("com.example.Echo").Destination("com.example.Echo"));
  std::cout << "Sending fourth message\n";
  co_await conn->SendMessage(DBusMessage::Create("EchoMethod").Path(ObjectPath{"/echo"}).Interface("com.example.Echo").Destination("com.example.Echo"));
}

boost::asio::awaitable<void> DBusReceiveMessagesTest(std::shared_ptr<DBusConnection> conn, boost::asio::io_context& ioService)
{
  conn->ReceiveIncomingMessages([](IncomingDBusMessage message) { std::cout << "Message Received! " << message.GetHeader().GetMember().value() << "\n"; });

  boost::asio::system_timer timer{ioService};
  timer.expires_after(std::chrono::seconds(5));
  co_await timer.async_wait(boost::asio::use_awaitable);

  co_return;
}

boost::asio::awaitable<void> async_main(boost::asio::io_context& ioService)
{
  std::shared_ptr<DBusConnection> conn{co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, CreateConnectionDetached::YES)};
  std::cout << "Created connection\n";

  // co_await DBusEchoTest(conn);
  co_await DBusReceiveMessagesTest(conn, ioService);
}

int main()
{
  boost::asio::io_context ioService;

  boost::asio::co_spawn(ioService, async_main(ioService),
                        [](std::exception_ptr e)
                        {
                          if (e) std::rethrow_exception(e);
                        });

  ioService.run();
}
