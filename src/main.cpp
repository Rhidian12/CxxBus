#include <boost/asio/io_context.hpp>
#include <exception>
#include <iostream>

#include "DBusConnection.h"
#include "DBusTypes.h"

boost::asio::awaitable<void> async_main(boost::asio::io_context& ioService)
{
  std::shared_ptr<DBusConnection> conn{co_await DBusConnection::Create(ioService, DBusWellKnownName{"com.dbus.CxxTest"}, CreateConnectionDetached::YES)};
  std::cout << "Created connection\n";

  std::cout << "Sending first message\n";
  co_await conn->SendMessage(DBusMessage{"EchoMethod", ObjectPath{"/echo"}, "com.example.Echo", "com.example.Echo"});
  std::cout << "Sending second message\n";
  co_await conn->SendMessage(DBusMessage{"EchoMethod", ObjectPath{"/echo"}, "com.example.Echo", "com.example.Echo"});
  std::cout << "Sending third message\n";
  co_await conn->SendMessage(DBusMessage{"EchoMethod", ObjectPath{"/echo"}, "com.example.Echo", "com.example.Echo"});
  std::cout << "Sending fourth message\n";
  co_await conn->SendMessage(DBusMessage{"EchoMethod", ObjectPath{"/echo"}, "com.example.Echo", "com.example.Echo"});
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
