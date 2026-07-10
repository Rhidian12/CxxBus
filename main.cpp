#include <boost/asio/io_context.hpp>
#include "DBusConnection.h"

int main()
{
    boost::asio::io_context ioService;
    DBusConnection conn{ioService};

    ioService.run();
}