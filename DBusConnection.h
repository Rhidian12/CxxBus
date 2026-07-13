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
  boost::asio::io_context & m_ioContext;
  boost::asio::local::stream_protocol::socket m_socket;

  // Store channels to make our 'SendMessage' be awaitable
  std::map<uint32_t, boost::asio::experimental::channel<void(boost::system::error_code, DBusReply)>*> m_replyChannels;

  // Send messages to the SendLoop() coroutine
  boost::asio::experimental::channel<void(boost::system::error_code,
                                          std::tuple<DBusMessage, uint32_t>)>
      m_sendLoop;

  uint32_t m_serial;
  std::string m_uniqueConnection;
  std::string m_wellKnownName;

private:
  boost::asio::awaitable<void> AuthenticateDBusConnection();
  boost::asio::awaitable<void> Connect();
  boost::asio::awaitable<void> SendLoop();
  boost::asio::awaitable<void> ReadLoop();

public:
  DBusConnection(boost::asio::io_context & ioService)
      : m_ioContext(ioService), m_socket(m_ioContext), m_sendLoop(m_ioContext, 10),
        m_serial{1}, m_uniqueConnection{} {
    boost::asio::co_spawn(m_ioContext, Connect(), [](std::exception_ptr e) {
      if (e) {
        try {
          std::rethrow_exception(e);
        } catch (const std::exception &ex) {
          std::cerr << "Error in Connect coroutine: " << ex.what() << "\n";
          throw;
        }
      }
    });
  }

  boost::asio::awaitable<std::optional<DBusReply>> SendMessage(DBusMessage const &message);

  ~DBusConnection() { ::unlink("/tmp/dbus-test"); }
};
