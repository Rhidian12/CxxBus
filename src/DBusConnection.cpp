#include "DBusConnection.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <tuple>

#include "DBus.h"
#include "DBusMessage.h"
#include "DBusReply.h"
#include "DBusTypes.h"
#include "Log.h"

namespace
{
#ifndef CXX_BUS_LOGLEVEL
#define CXX_BUS_LOGLEVEL Error
#endif // CXX_BUS_LOGLEVEL
  Logger const LOGGER {.logLevel = LogLevel::CXX_BUS_LOGLEVEL};

  std::string ParseDBusAddress()
  {
    // Looks something like: unix:path=/run/user/1000/bus
    std::string_view const dbusAddress{getenv("DBUS_SESSION_BUS_ADDRESS")};

    if (!dbusAddress.starts_with("unix:"))
    {
      throw std::runtime_error{"Only support unix sockets for DBus-daemon connections"};
    }

    LOGGER.LogInfo(std::format("DBus address: {}", dbusAddress));

    return std::string{dbusAddress.substr(dbusAddress.find("=") + 1)};
  }

  std::string HexEncodeString(std::string const& str)
  {
    std::string newStr;
    std::ranges::for_each(str, [&newStr](unsigned char c) { newStr += std::format("{:x}", c); });
    return newStr;
  }
}  // namespace

boost::asio::awaitable<std::shared_ptr<DBusConnection>> DBusConnection::Create(boost::asio::io_context& ioService, DBusWellKnownName wellKnownName,
                                                                               CreateConnectionDetached connectionMethod)
{
  std::shared_ptr<DBusConnection> conn{new DBusConnection(ioService, std::move(wellKnownName))};

  if (connectionMethod == CreateConnectionDetached::YES)
  {
    boost::asio::co_spawn(ioService, conn->Connect(),
                          [](std::exception_ptr e)
                          {
                            if (e)
                            {
                              std::rethrow_exception(e);
                            }
                          });
  }
  else
  {
    co_await conn->Connect();
  }

  co_return conn;
}

DBusConnection::DBusConnection(boost::asio::io_context& ioService, DBusWellKnownName wellKnownName)
  : m_ioContext(ioService)
  , m_state(
        new InternalState{.socket = boost::asio::local::stream_protocol::socket{m_ioContext},
                          .replyChannels = std::map<uint32_t, boost::asio::experimental::channel<void(boost::system::error_code, DBusMessage)>*>{},
                          .onIncomingSignal = {},
                          .sendLoop = boost::asio::experimental::channel<void(boost::system::error_code, std::tuple<DBusMessage, uint32_t>)>{m_ioContext, 10},
                          .connectionReady = false,
                          .connectionCompleted = boost::asio::experimental::channel<void(boost::system::error_code)>{m_ioContext},
                          .nrOfWaiters = 0,
                          .serial = 1,
                          .uniqueConnection = "",
                          .wellKnownName = std::move(wellKnownName)})
{
}

DBusConnection::~DBusConnection()
{
  boost::system::error_code ec;
  std::ignore = m_state->socket.close(ec);
  m_state->sendLoop.close();
}

boost::asio::awaitable<void> DBusConnection::AuthenticateDBusConnection()
{
  // First send a single '\0' byte
  co_await m_state->socket.async_send(boost::asio::buffer("\0", 1), boost::asio::use_awaitable);

  // Next we must authenticate ourselves, we use the EXTERNAL
  // authentication method
  co_await m_state->socket.async_send(boost::asio::buffer(std::format("AUTH EXTERNAL {}\r\n", HexEncodeString(std::to_string(::getuid())))),
                                      boost::asio::use_awaitable);

  // Now we expect to see OK <guid>
  std::string reply{};
  co_await boost::asio::async_read_until(m_state->socket, boost::asio::dynamic_buffer(reply), "\r\n", boost::asio::use_awaitable);

  if (!reply.starts_with("OK"))
  {
    throw std::runtime_error{"Authentication failed!"};
  }

  // Yippee! All worked, so now start our DBus Connection!
  co_await m_state->socket.async_send(boost::asio::buffer("BEGIN\r\n", 7), boost::asio::use_awaitable);
}

boost::asio::awaitable<void> DBusConnection::Connect()
{
  // Connect to DBus daemon
  boost::asio::local::stream_protocol::endpoint endpoint{ParseDBusAddress()};
  co_await m_state->socket.async_connect(endpoint, boost::asio::as_tuple(boost::asio::use_awaitable));

  co_await AuthenticateDBusConnection();

  LOGGER.LogTrace("Connected to DBus-daemon. Starting Send loop");
  boost::asio::co_spawn(m_ioContext, SendLoop(), boost::asio::detached);

  LOGGER.LogTrace("Send loop started. Starting Read loop");
  boost::asio::co_spawn(m_ioContext, ReadLoop(), boost::asio::detached);
  
  LOGGER.LogTrace("Read loop started. Starting connection handshake");
  // Get our unique bus name
  std::optional<DBusMessage> reply = co_await SendMessageInternal(
      DBusMessage::Create("Hello").Path(ObjectPath{"/org/freedesktop/DBus"}).Interface("org.freedesktop.DBus").Destination("org.freedesktop.DBus"));
  if (reply.has_value())
  {
    m_state->uniqueConnection = reply->Get<std::string>();
  }

  LOGGER.LogInfo(std::format("Unique Connection ID: {}", m_state->uniqueConnection));
  
  // Now, request a well-known name from the dbus-daemon
  reply =
      co_await SendMessageInternal(DBusMessage::Create("RequestName")
                                       .Path(ObjectPath{"/org/freedesktop/DBus"})
                                       .Interface("org.freedesktop.DBus")
                                       .Destination("org.freedesktop.DBus")
                                       .Parameter(MultipleCompleteTypes<std::string, uint32_t>{m_state->wellKnownName.GetName(), static_cast<uint32_t>(0x1)}));

  // [TODO]: We're expecting a reply, not getting a reply is an error and we shouldn't have to explicitly check that
  if (reply.has_value())
  {
    // [TODO]: Handle errors here
    if (reply->Get<uint32_t>() == 1)
    {
    }
  }

  LOGGER.LogTrace("Connection handshake completed.");
  m_state->connectionReady = true;
  if (m_state->nrOfWaiters > 0)
  {
    co_await m_state->connectionCompleted.async_send(boost::system::error_code{}, boost::asio::use_awaitable);
  }
}

boost::asio::awaitable<void> DBusConnection::SendLoop()
{
  std::weak_ptr<DBusConnection> weakThis{shared_from_this()};
  if (weakThis.expired())
  {
    co_return;
  }

  auto state = m_state;

  while (!weakThis.expired())
  {
    try
    {
      // Wait for an incoming message to send
      auto [message, serial] = co_await state->sendLoop.async_receive(boost::asio::use_awaitable);
      co_await boost::asio::async_write(state->socket, boost::asio::buffer(message.Serialize(serial)), boost::asio::use_awaitable);

      LOGGER.LogTrace(std::format("Sent message with method '{}' and serial '{}' to path '{}' with interface '{}'", message.GetMember(), serial,
                                  std::string{message.GetPath()}, std::string{message.GetInterface()}));
    }
    catch (boost::system::system_error const& ex)
    {
      if (ex.code().category().name() == std::string{"asio.channel"} && ex.code().value() == 1)
      {
        // Channel closed, exit the loop
        break;
      }
      else if (ex.code().category().name() == std::string{"system"} && ex.code().value() == 125)
      {
        // Operation cancelled. Exit the loop
        break;
      }

      throw;  // rethrow the exception if it's not one of the expected ones
    }
    catch (std::exception const& ex)
    {
      LOGGER.LogError(std::format("Error occured in message send loop: {}", ex.what()));
    }
  }
}

boost::asio::awaitable<void> DBusConnection::ReadLoop()
{
  std::weak_ptr<DBusConnection> weakThis{shared_from_this()};
  if (weakThis.expired())
  {
    co_return;
  }

  std::vector<byte> rawFullReply{};
  auto state = m_state;

  while (!weakThis.expired())
  {
    try
    {
      rawFullReply.clear();

      std::vector<byte> tempBuffer{};
      tempBuffer.resize(FIRST_HEADER_PART_SIZE);
      co_await boost::asio::async_read(state->socket, boost::asio::buffer(tempBuffer), boost::asio::use_awaitable);
      rawFullReply.append_range(tempBuffer);
      DBusMessageHeader messageHeader{std::move(tempBuffer)};

      tempBuffer.resize(sizeof(uint32_t));
      co_await boost::asio::async_read(state->socket, boost::asio::buffer(tempBuffer), boost::asio::use_awaitable);
      rawFullReply.append_range(tempBuffer);
      messageHeader.ParseHeaderFieldLength(std::move(tempBuffer));

      tempBuffer.resize(messageHeader.GetHeaderFieldsLength());
      co_await boost::asio::async_read(state->socket, boost::asio::buffer(tempBuffer), boost::asio::use_awaitable);
      rawFullReply.append_range(std::move(tempBuffer));

      uint32_t arrPointer{FIRST_HEADER_PART_SIZE};
      messageHeader.ParseRemainderOfHeader(rawFullReply, arrPointer);

      uint32_t const oldArrPointer{arrPointer};
      AddPaddingToSize(arrPointer, DBUS_MESSAGE_BODY_ALIGNMENT);
      uint32_t const nrOfPaddingBytes{arrPointer - oldArrPointer};

      tempBuffer.resize(nrOfPaddingBytes + messageHeader.GetMessageLength());
      co_await boost::asio::async_read(state->socket, boost::asio::buffer(tempBuffer), boost::asio::use_awaitable);

      // Skip over the padding, we don't care about it
      DBusMessage message{std::move(messageHeader), std::ranges::to<std::vector>(tempBuffer | std::views::drop(nrOfPaddingBytes))};

      if (message.GetHeader().GetReplySerial().has_value())
      {
        LOGGER.LogTrace(std::format("Received reply to message with serial '{}'. Signature of reply: '{}'", *message.GetHeader().GetReplySerial(),
                                    std::string{message.GetHeader().GetSignature().value_or(Signature{""})}));

        if (!state->replyChannels.contains(*message.GetHeader().GetReplySerial()))
        {
          // It should not be possible to get a reply to a message we don't know
          LOGGER.LogFatal(std::format("Received a reply with serial '{}' but we do not have the serial of the original message",
                                      message.GetHeader().GetReplySerial().value()));
          throw std::runtime_error{"Internal error: Receiving reply to a message, but the serial is unknown to us"};
        }
        co_await state->replyChannels[*message.GetHeader().GetReplySerial()]->async_send(boost::system::error_code{}, message);
      }
      else
      {
        LOGGER.LogTrace(std::format("Received incoming message with serial '{}' and signature '{}'", message.GetHeader().GetSerial(),
                                    std::string{message.GetHeader().GetSignature().value()}));

        // If it has no reply serial, then it means it's an incoming call
        if (!state->onIncomingSignal.empty())
        {
          state->onIncomingSignal(message);
        }
      }
    }
    catch (boost::system::system_error const& ex)
    {
      if (ex.code().category().name() == std::string{"asio.channel"} && ex.code().value() == 1)
      {
        // Channel closed, exit the loop
        break;
      }
      else if (ex.code().category().name() == std::string{"system"} && ex.code().value() == 125)
      {
        // Operation cancelled. Exit the loop
        break;
      }

      throw;  // rethrow the exception if it's not one of the expected ones
    }
    catch (std::exception const& ex)
    {
      LOGGER.LogError(std::format("Error occured in message read loop: {}", ex.what()));
    }
  }
}

boost::asio::awaitable<std::optional<DBusMessage>> DBusConnection::SendMessageInternal(DBusMessage const& message)
{
  // 1st, if we're expecting a reply, store a channel so we can await a reply from the dbus-daemon
  boost::asio::experimental::channel<void(boost::system::error_code, DBusMessage)> replyChannel{m_ioContext, 1};

  if (!std::ranges::contains(message.GetFlags(), DBusMessageFlags::NO_REPLY_EXPECTED))
  {
    m_state->replyChannels[m_state->serial] = &replyChannel;
  }

  // 2nd, send our message to the SendLoop() coroutine to actually send the message
  co_await m_state->sendLoop.async_send(boost::system::error_code{}, std::make_tuple(message, m_state->serial++), boost::asio::use_awaitable);

  // [TODO]: If we don't have a channel, don't wait on it, just return std::nullopt
  // 3rd, wait for the reply to be sent back to us from the ReadLoop() coroutine
  DBusMessage reply = co_await replyChannel.async_receive(boost::asio::use_awaitable);
  m_state->replyChannels.erase(reply.GetHeader().GetReplySerial().value());

  if (reply.GetHeader().GetMessageType() == DBusMessageType::ERROR)
  {
    // We got an error, so throw an error here
    throw DBusError{std::format("DBus Error Reply received: {}", reply.HasArguments() && reply.GetHeader().GetSignature() == "s"
                                                                     ? std::format("Error Message: {}", reply.Get<std::string>())
                                                                     : "No error message was provided by the remote")};
  }

  co_return reply;
}

boost::asio::awaitable<std::optional<DBusMessage>> DBusConnection::SendMessage(DBusMessage const& message)
{
  // Wait until our Connnection is ready
  if (!m_state->connectionReady)
  {
    LOGGER.LogTrace("Connection not ready yet, waiting for it to complete");
    m_state->nrOfWaiters++;
    co_await m_state->connectionCompleted.async_receive(boost::asio::use_awaitable);
  }

  co_return co_await SendMessageInternal(message);
}

void DBusConnection::ReceiveIncomingMessages(std::function<void(DBusMessage)> callback)
{
  m_state->onIncomingSignal.connect(std::move(callback));
}
