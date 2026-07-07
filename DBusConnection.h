#pragma once

#include "DBusMessage.h"
#include "DBusReply.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <queue>
#include <unistd.h>

#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <cstdlib>
#include <format>
#include <optional>
#include <stdexcept>

class DBusConnection {
private:
  boost::asio::io_context m_ioContext;
  boost::asio::ip::udp::socket m_socket;

  // Send a channel to send replies back to the user to the ReadLoop() coroutine
  boost::asio::experimental::channel<void(boost::system::error_code, boost::asio::experimental::channel<void(boost::system::error_code, DBusReply)>*)>
      m_replyChannel;

  // Send messages to the SendLoop() coroutine
  boost::asio::experimental::channel<void(boost::system::error_code,
                                          DBusMessage)>
      m_sendLoop;

  uint32_t m_serial;
  std::string m_uniqueConnection;

private:
  boost::asio::awaitable<void> AuthenticateDBusConnection();
  boost::asio::awaitable<void> Connect();
  boost::asio::awaitable<void> SendLoop();
  boost::asio::awaitable<void> ReadLoop();

public:
  DBusConnection(std::optional<std::string> dbusEndpoint = std::nullopt)
      : m_ioContext(), m_socket(m_ioContext), m_sendLoop(m_ioContext, 10),
        m_replyChannel(m_ioContext, 10), m_serial{1}, m_uniqueConnection{} {
    boost::asio::co_spawn(m_ioContext, Connect(), boost::asio::detached);
  }

  boost::asio::awaitable<std::optional<DBusReply>> SendMessage(DBusMessage const &message);

  ~DBusConnection() { ::unlink("/tmp/dbus-test"); }
};
