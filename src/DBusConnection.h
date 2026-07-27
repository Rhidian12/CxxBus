#pragma once

#include <unistd.h>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/signals2.hpp>
#include <boost/system/detail/error_code.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>

#include "DBusMessage.h"
#include "DBusTypes.h"
#include "IncomingDBusMessage.h"

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
  struct InternalState
  {
    boost::asio::local::stream_protocol::socket socket;

    // Store channels to make our 'SendMessage' be awaitable
    std::map<uint32_t, boost::asio::experimental::channel<void(boost::system::error_code, IncomingDBusMessage)>*> replyChannels;
    boost::signals2::signal<void(IncomingDBusMessage)> onIncomingSignal;

    // Send messages to the SendLoop() coroutine
    boost::asio::experimental::channel<void(boost::system::error_code, std::tuple<DBusMessage, uint32_t>)> sendLoop;

    bool connectionReady;
    boost::asio::experimental::channel<void(boost::system::error_code)> connectionCompleted;
    int nrOfWaiters; // Number of coroutines waiting for the connection to be ready

    uint32_t serial;
    std::string uniqueConnection;
    DBusWellKnownName wellKnownName;
  };

 private:
  boost::asio::io_context& m_ioContext;
  std::shared_ptr<InternalState> m_state;

 private:
  boost::asio::awaitable<void> AuthenticateDBusConnection();
  boost::asio::awaitable<void> Connect();
  boost::asio::awaitable<void> SendLoop();
  boost::asio::awaitable<void> ReadLoop();

 private:
  DBusConnection(boost::asio::io_context& ioService, DBusWellKnownName wellKnownName);

  // Does not wait for the connection to be ready -> Can be used internally to set up the connection.
  // Prefer 'SendMessage()' whenever possible
  boost::asio::awaitable<std::optional<IncomingDBusMessage>> SendMessageInternal(DBusMessage const& message);

 public:
  ~DBusConnection();
  static boost::asio::awaitable<std::shared_ptr<DBusConnection>> Create(boost::asio::io_context& ioService, DBusWellKnownName wellKnownName,
                                                                        CreateConnectionDetached connectionMethod);

  void ReceiveIncomingMessages(std::function<void(IncomingDBusMessage)> callback);
  boost::asio::awaitable<std::optional<IncomingDBusMessage>> SendMessage(DBusMessage const& message);
};
