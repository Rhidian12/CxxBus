#include <sys/types.h>

#include <boost/asio/completion_condition.hpp>
#include <boost/asio/detached.hpp>
#include <boost/system/detail/error_code.hpp>
#include <cstdint>

#include "DBusConnection.h"
#include "DBusTypes.h"
#include "IncomingDBusMessage.h"

namespace cxxbus
{
  namespace
  {
#ifndef CXX_BUS_LOGLEVEL
#define CXX_BUS_LOGLEVEL ERROR
#endif  // CXX_BUS_LOGLEVEL

    Logger const LOGGER{.logLevel = LogLevel::CXX_BUS_LOGLEVEL};
  }  // namespace

  boost::asio::awaitable<void> DBusConnection::ReadLoop()
  {
    std::vector<byte> rawFullReply{};
    rawFullReply.reserve(1028);
    auto state = m_state;

    while (!state->shouldQuit)
    {
      try
      {
        rawFullReply.clear();

        co_await boost::asio::async_read(*state->socket, boost::asio::dynamic_buffer(rawFullReply),
                                         boost::asio::transfer_exactly(FIRST_HEADER_PART_SIZE),
                                         boost::asio::use_awaitable);

        DBusMessageHeader messageHeader{rawFullReply};

        co_await boost::asio::async_read(*state->socket, boost::asio::dynamic_buffer(rawFullReply),
                                         boost::asio::transfer_exactly(sizeof(uint32_t)), boost::asio::use_awaitable);

        messageHeader.ParseHeaderFieldLength(
            std::span<byte>{rawFullReply.begin() + FIRST_HEADER_PART_SIZE, rawFullReply.end()});

        uint32_t const headerFieldLength = messageHeader.GetHeaderFieldsLength();
        co_await boost::asio::async_read(*state->socket, boost::asio::dynamic_buffer(rawFullReply),
                                         boost::asio::transfer_exactly(headerFieldLength), boost::asio::use_awaitable);

        uint32_t arrPointer{FIRST_HEADER_PART_SIZE};
        messageHeader.ParseRemainderOfHeader(rawFullReply, arrPointer);

        uint32_t const oldArrPointer{arrPointer};
        AddPaddingToSize(arrPointer, DBUS_MESSAGE_BODY_ALIGNMENT);
        uint32_t const nrOfPaddingBytes{arrPointer - oldArrPointer};

        co_await boost::asio::async_read(
            *state->socket, boost::asio::dynamic_buffer(rawFullReply),
            boost::asio::transfer_exactly(nrOfPaddingBytes + messageHeader.GetMessageLength()),
            boost::asio::use_awaitable);

        // Skip over the padding, we don't care about it
        IncomingDBusMessage message{
            std::move(messageHeader),
            std::ranges::to<std::vector>(rawFullReply | std::views::drop(FIRST_HEADER_PART_SIZE + sizeof(uint32_t) +
                                                                         headerFieldLength + nrOfPaddingBytes))};
        co_await HandleReadMessage(std::move(message));
      }
      catch (boost::system::system_error const& ex)
      {
        if (state->shouldQuit)
        {
          break;
        }

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

        // Any other socket error (e.g. EOF, connection reset, broken pipe) means the connection to the dbus-daemon was
        // lost unexpectedly. Report it and handle the connection loss.
        LOG_ERROR(LOGGER, "Read loop lost connection to dbus-daemon: {}", ex.what());
        boost::asio::co_spawn(*state->strand, HandleConnectionLost(), boost::asio::detached);
        break;
      }
      catch (std::exception const& ex)
      {
        LOG_ERROR(LOGGER, "Error occured in message read loop: {}", ex.what());
      }
    }

    LOG_TRACE(LOGGER, "Read Loop is quitting gracefully");
    state->readLoopFinished.async_send(boost::system::error_code{}, boost::asio::detached);
  }

  boost::asio::awaitable<void> DBusConnection::SendLoop()
  {
    auto state = m_state;

    while (!state->shouldQuit)
    {
      try
      {
        // Wait for an incoming message to send
        auto [message, serial, messageSentChannel] = co_await state->sendLoop.async_receive(
            boost::asio::bind_executor(*state->strand, boost::asio::use_awaitable));
        co_await boost::asio::async_write(*state->socket, boost::asio::buffer(message.Serialize(serial)),
                                          boost::asio::use_awaitable);

        std::string const info = message.GetInfo();
        LOG_TRACE(LOGGER, "Sent message '{}' with serial '{}'", info, serial);
        if (!message.ExpectsReply())
        {
          co_await messageSentChannel->async_send(boost::system::error_code{}, boost::asio::use_awaitable);
        }
      }
      catch (boost::system::system_error const& ex)
      {
        if (state->shouldQuit)
        {
          break;
        }

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

        // Any other socket error (e.g. EOF, connection reset, broken pipe) means the connection to the dbus-daemon was
        // lost unexpectedly. Report it and handle the connection loss.
        LOG_ERROR(LOGGER, "Send loop lost connection to dbus-daemon: {}", ex.what());
        boost::asio::co_spawn(*state->strand, HandleConnectionLost(), boost::asio::detached);
        break;
      }
      catch (std::exception const& ex)
      {
        LOG_ERROR(LOGGER, "Error occured in message send loop: {}", ex.what());
      }
    }

    LOG_TRACE(LOGGER, "Send Loop is quitting gracefully");
    state->sendLoopFinished.async_send(boost::system::error_code{}, boost::asio::detached);
  }

}  // namespace cxxbus
