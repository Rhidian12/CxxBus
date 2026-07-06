#pragma once

#include <unistd.h>

#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <cstdlib>
#include <format>
#include <optional>
#include <stdexcept>

class DBusConnection
{
 private:
  boost::asio::io_context m_ioContext;
  boost::asio::ip::udp::socket m_socket;

 private:
  std::string ParseDBusAddress()
  {
    // Looks something like: unix:path=/run/user/1000/bus
    std::string_view const dbusAddress{getenv("DBUS_SESSION_BUS_ADDRESS")};

    if (!dbusAddress.starts_with("unix:"))
    {
      throw std::runtime_error{"Only support unix sockets for DBus-daemon connections"};
    }

    return std::string{dbusAddress.substr(dbusAddress.find("=") + 1)};
  }

  std::string HexEncodeString(std::string const& str)
  {
    std::string newStr;
    std::ranges::for_each(str, [&newStr](unsigned char c) { newStr += std::format("{:x}", c); });
    return newStr;
  }

 public:
  DBusConnection(std::optional<std::string> dbusEndpoint = std::nullopt)
    : m_ioContext()
    , m_socket(m_ioContext)
  {
    // Connect to DBus daemon
    boost::asio::ip::udp::endpoint endpoint{boost::asio::ip::make_address(ParseDBusAddress()), 0};
    m_socket.connect(endpoint);

    // First send a single '\0' byte
    m_socket.send('\0');

    // Next we must authenticate ourselves, we use the EXTERNAL authentication method
    m_socket.send(std::format("AUTH EXTERNAL {}\r\n", HexEncodeString(std::to_string(::getuid()))));

    // Now we expect to see OK <guid>
    std::string reply{};
    m_socket.receive(boost::asio::buffer(reply));

    if (!reply.starts_with("OK"))
    {
      throw std::runtime_error{"Authentication failed!"};
    }

    // Yippee! All worked, so now start our DBus Connection!
    m_socket.send("BEGIN\r\n");
  }

  ~DBusConnection()
  {
    ::unlink("/tmp/dbus-test");
  }
};
