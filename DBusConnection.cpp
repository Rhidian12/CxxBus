#include "DBusConnection.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/read_until.hpp>
#include <iostream>
#include <tuple>

#include "DBus.h"
#include "DBusReply.h"
#include "DBusTypes.h"

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

    std::cout << "Got DBus address: " << dbusAddress.substr(dbusAddress.find("=") + 1) << "\n";

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
  co_await m_socket.async_send(boost::asio::buffer(std::format("AUTH EXTERNAL {}\r\n", HexEncodeString(std::to_string(::getuid())))),
                               boost::asio::use_awaitable);

  // Now we expect to see OK <guid>
  std::string reply{};
  co_await boost::asio::async_read_until(m_socket, boost::asio::dynamic_buffer(reply), "\r\n", boost::asio::use_awaitable);

  std::cout << "Got reply: " << reply << "\n";

  if (!reply.starts_with("OK"))
  {
    throw std::runtime_error{"Authentication failed!"};
  }

  // Yippee! All worked, so now start our DBus Connection!
  co_await m_socket.async_send(boost::asio::buffer("BEGIN\r\n"), boost::asio::use_awaitable);
}

boost::asio::awaitable<void> DBusConnection::Connect()
{
  std::cout << "Starting Connect!\n";

  // Connect to DBus daemon
  boost::asio::local::stream_protocol::endpoint endpoint{ParseDBusAddress()};
  std::cout << "Connecting\n";
  co_await m_socket.async_connect(endpoint, boost::asio::use_awaitable);

  co_await AuthenticateDBusConnection();

  std::cout << "Connected to DBus-daemon! Starting Send Loop!\n";

  boost::asio::co_spawn(m_ioContext, SendLoop(), boost::asio::detached);

  std::cout << "Starting Read Loop!\n";
  boost::asio::co_spawn(m_ioContext, ReadLoop(), boost::asio::detached);

  // Get our unique bus name
  std::optional<DBusReply> reply = co_await SendMessage(DBusMessage{"Hello", ObjectPath{"/org/freedesktop/DBus"}, "org.freedesktop.DBus"});
  if (reply.has_value())
  {
    m_uniqueConnection = reply->Get<std::string>();
  }

  std::cout << "Unique Connection ID: " << m_uniqueConnection << "\n";

  // [TODO]: Should be a user-passed flag
  // Now, request a well-known name from the dbus-daemon
  DBusMessage message{MultipleCompleteTypes<std::string, uint32_t>{std::string{"RhidianTest"}, static_cast<uint32_t>(0x1)}, "RequestName",
                      ObjectPath{"/org/freedesktop/DBus"}, "org.freedesktop.DBus"};
  reply = co_await SendMessage(message);

  if (reply.has_value())
  {
    if (reply->Get<uint32_t>() == 1)
    {
      m_wellKnownName = "RhidianTest";

      std::cout << "Well-known name: " << m_wellKnownName << "\n";
    }
  }
}

boost::asio::awaitable<void> DBusConnection::SendLoop()
{
  while (true)
  {
    std::cout << "Send loop is waiting for a message to send\n";
    auto [message, serial] = co_await m_sendLoop.async_receive(boost::asio::use_awaitable);

    std::cout << "Got a message, sending it now...\n";

    co_await m_socket.async_send(boost::asio::buffer(message.Serialize(serial)), boost::asio::use_awaitable);
  }
}

boost::asio::awaitable<void> DBusConnection::ReadLoop()
{
  try
  {
    while (true)
    {
      // std::vector<byte> rawReply{};
      // rawReply.reserve(FIRST_HEADER_PART_SIZE);

      // std::cout << "Waiting for incoming message\n";
      // co_await m_socket.async_receive(boost::asio::buffer(rawReply.data(), rawReply.capacity()), boost::asio::use_awaitable);
      // DBusMessageHeader messageHeader{std::move(rawReply)};

      // std::cout << "Got first part of header\n";
      // rawReply.reserve(messageHeader.GetHeaderFieldsLength());
      // co_await m_socket.async_receive(boost::asio::buffer(rawReply.data(), rawReply.capacity()), boost::asio::use_awaitable);
      // messageHeader.ParseRemainderOfHeader(std::move(rawReply));

      // std::cout << "Got rest of header\n";
      // // Calculate how many bytes we still have to read from the socket to complete our DBus Message.
      // // This is the amount of padding bytes until the start of our message body + message length
      // uint32_t const readBytes{FIRST_HEADER_PART_SIZE + messageHeader.GetHeaderFieldsLength()};
      // uint32_t nrOfPaddingBytes{readBytes};
      // AddPaddingToSize(nrOfPaddingBytes, 8);  // [TODO]: Dont hardcode this

      // // 'nrOfPaddingBytes - readBytes' to calculate how many padding bytes we have
      // rawReply.reserve((nrOfPaddingBytes - readBytes) + messageHeader.GetMessageLength());
      // co_await m_socket.async_receive(boost::asio::buffer(rawReply.data(), rawReply.capacity()), boost::asio::use_awaitable);

      // std::cout << "Got entire message\n";
      // // Skip over the padding, we don't care about it
      // DBusReply reply{std::move(messageHeader), std::ranges::to<std::vector>(rawReply | std::views::drop(nrOfPaddingBytes - readBytes))};

      // if (reply.GetReplySerial().has_value())
      // {
      //   if (m_replyChannels.contains(*reply.GetReplySerial()))
      //   {
      //     co_await m_replyChannels[*reply.GetReplySerial()]->async_send(boost::system::error_code{}, reply);
      //   }
      // }
    }
  }
  catch (std::exception const& ex)
  {
    std::cerr << "Error occurred in ReadLoop: " << ex.what() << "\n";
  }
}

boost::asio::awaitable<std::optional<DBusReply>> DBusConnection::SendMessage(DBusMessage const& message)
{
  // 1st, if we're expecting a reply, store a channel so we can await a reply from the dbus-daemon
  boost::asio::experimental::channel<void(boost::system::error_code, DBusReply)> replyChannel{m_ioContext, 1};

  if (!std::ranges::contains(message.GetFlags(), DBusMessageFlags::NO_REPLY_EXPECTED))
  {
    m_replyChannels[m_serial] = &replyChannel;
  }
  
  // 2nd, send our message to the SendLoop() coroutine to actually send the message
  co_await m_sendLoop.async_send(boost::system::error_code{}, std::make_tuple(message, m_serial++), boost::asio::use_awaitable);
  
  // [TODO]: If we don't have a channel, don't wait on it, just return std::nullopt
  // 3rd, wait for the reply to be sent back to us from the ReadLoop() coroutine
  co_return co_await replyChannel.async_receive(boost::asio::use_awaitable);
}
