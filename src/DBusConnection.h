#pragma once

#include <unistd.h>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/detail/error_code.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>

#include "DBusMessage.h"
#include "DBusTypes.h"

class DBusError : public std::runtime_error
{
 public:
  using std::runtime_error::runtime_error;
};

enum class CreateConnectionDetached : uint8_t
{
  NO = 0,
  YES = 1,
};

class DBusConnection : public std::enable_shared_from_this<DBusConnection>
{
 private:
  boost::asio::io_context& m_ioContext;
  std::shared_ptr<boost::asio::local::stream_protocol::socket> m_socket;

  // Store channels to make our 'SendMessage' be awaitable
  std::shared_ptr<std::map<uint32_t, boost::asio::experimental::channel<void(boost::system::error_code, DBusMessage)>*>> m_replyChannels;

  // Send messages to the SendLoop() coroutine
  std::shared_ptr<boost::asio::experimental::channel<void(boost::system::error_code, std::tuple<DBusMessage, uint32_t>)>> m_sendLoop;

  bool m_connectionReady;
  std::shared_ptr<boost::asio::experimental::channel<void(boost::system::error_code)>> m_connectionCompleted;

  uint32_t m_serial;
  std::string m_uniqueConnection;
  DBusWellKnownName m_wellKnownName;

 private:
  boost::asio::awaitable<void> AuthenticateDBusConnection();
  boost::asio::awaitable<void> Connect();
  boost::asio::awaitable<void> SendLoop();
  boost::asio::awaitable<void> ReadLoop();

 private:
  DBusConnection(boost::asio::io_context& ioService, DBusWellKnownName wellKnownName);

  // Does not wait for the connection to be ready -> Can be used internally to set up the connection.
  // Prefer 'SendMessage()' whenever possible
  boost::asio::awaitable<std::optional<DBusMessage>> SendMessageInternal(DBusMessage const& message);

 public:
  static boost::asio::awaitable<std::shared_ptr<DBusConnection>> Create(boost::asio::io_context& ioService, DBusWellKnownName wellKnownName,
                                                                        CreateConnectionDetached connectionMethod);

  boost::asio::awaitable<std::optional<DBusMessage>> SendMessage(DBusMessage const& message);

  ~DBusConnection()
  {
    ::unlink("/tmp/dbus-test");
  }
};
