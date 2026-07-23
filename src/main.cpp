#include <boost/asio/io_context.hpp>
#include <iostream>

#include "DBusConnection.h"
#include "DBusTypes.h"

int main()
{
  boost::asio::io_context ioService;
  DBusConnection conn{ioService, DBusWellKnownName{"com.dbus.CxxTest"}};

  std::cout << "Running IOService\n";

  ioService.run();

  std::cout << "IOService finished\n";
}
