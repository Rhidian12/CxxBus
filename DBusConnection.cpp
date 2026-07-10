#include "DBusConnection.h"

#include <boost/asio/awaitable.hpp>

namespace
{
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
}  // namespace

boost::asio::awaitable<void> DBusConnection::AuthenticateDBusConnection()
{
  // First send a single '\0' byte
  co_await m_socket.async_send(boost::asio::buffer("\0", 1), boost::asio::use_awaitable);

  // Next we must authenticate ourselves, we use the EXTERNAL
  // authentication method
  co_await m_socket.async_send(boost::asio::buffer(std::format("AUTH EXTERNAL {}\r\n", HexEncodeString(std::to_string(::getuid())))), boost::asio::use_awaitable);

  // Now we expect to see OK <guid>
  std::string reply{};
  co_await m_socket.async_receive(boost::asio::buffer(reply), boost::asio::use_awaitable);

  if (!reply.starts_with("OK"))
  {
    throw std::runtime_error{"Authentication failed!"};
  }

  // Yippee! All worked, so now start our DBus Connection!
  co_await m_socket.async_send(boost::asio::buffer("BEGIN\r\n"), boost::asio::use_awaitable);
}

boost::asio::awaitable<void> DBusConnection::Connect()
{
  // Connect to DBus daemon
  boost::asio::ip::udp::endpoint endpoint{boost::asio::ip::make_address(ParseDBusAddress()), 0};
  co_await m_socket.async_connect(endpoint, boost::asio::use_awaitable);

  co_await AuthenticateDBusConnection();

  boost::asio::co_spawn(m_ioContext, SendLoop(), boost::asio::detached);
  boost::asio::co_spawn(m_ioContext, ReadLoop(), boost::asio::detached);

  // Get our unique bus name
  std::optional<DBusReply> reply = co_await SendMessage(DBusMessage{"Hello", "/org/freedesktop/DBus", "org.freedesktop.DBus"});

  // [TODO]: Should be a user-passed flag
  // Now, request a well-known name from the dbus-daemon
  DBusMessage message{"RequestName", "/org/freedesktop/DBus", "org.freedesktop.DBus"};
  message.AddParameter(std::string{"RhidianTest"});
  message.AddParameter(static_cast<uint32_t>(0x1));  // AllowReplacement
  co_await SendMessage(message);
}

boost::asio::awaitable<void> DBusConnection::SendLoop()
{
  while (true)
  {
    auto message = co_await m_sendLoop.async_receive(boost::asio::use_awaitable);

    co_await m_socket.async_send(boost::asio::buffer(message.Serialize(m_serial)), boost::asio::use_awaitable);
  }
}

boost::asio::awaitable<void> DBusConnection::ReadLoop()
{
  while (true)
  {
    boost::asio::experimental::channel<void(boost::system::error_code, DBusReply)>* replyChannel =
        co_await m_replyChannel.async_receive(boost::asio::use_awaitable);

    std::vector<byte> reply{};
    co_await m_socket.async_receive(boost::asio::buffer(reply), boost::asio::use_awaitable);

    if (replyChannel)
    {
      co_await replyChannel->async_send(boost::system::error_code{}, DBusReply{reply});
    }
  }
}

boost::asio::awaitable<std::optional<DBusReply>> DBusConnection::SendMessage(DBusMessage const& message)
{
  // 1st, send our message to the SendLoop() coroutine to actually send the
  // message
  co_await m_sendLoop.async_send(boost::system::error_code{}, message, boost::asio::use_awaitable);

  // 2nd, if we're expecting a reply, send a channel to the ReadLoop() coroutine
  // to send the reply back to us
  boost::asio::experimental::channel<void(boost::system::error_code, DBusReply)> replyChannel{m_ioContext, 1};

  if (std::ranges::contains(message.GetFlags(), DBusMessageFlags::NO_REPLY_EXPECTED))
  {
    co_return std::nullopt;  // [TODO]: We should only return here once we
                             // actually sent the message
  }
  else
  {
    co_await m_replyChannel.async_send(boost::system::error_code{}, &replyChannel, boost::asio::use_awaitable);
  }

  // 3rd, wait for the reply to be sent back to us from the ReadLoop() coroutine
  co_return co_await replyChannel.async_receive(boost::asio::use_awaitable);
}
