#include <sys/types.h>

#include <boost/asio/completion_condition.hpp>
#include <boost/asio/detached.hpp>
#include <boost/system/detail/error_code.hpp>
#include <cstdint>

#include "DBus.h"
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

        // Read in the set 12 bytes of the DBus header + the 4 bytes of the following header field array
        co_await boost::asio::async_read(*state->socket, boost::asio::dynamic_buffer(rawFullReply),
                                         boost::asio::transfer_exactly(FIRST_HEADER_PART_SIZE),
                                         boost::asio::use_awaitable);

        auto headerData =
            UnmarshalDBusType<MultipleCompleteTypes<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t, uint32_t>>(
                rawFullReply, "yyyyuuu");
        uint32_t const messageLength = headerData.GetType<4>();
        uint32_t const headerFieldArrLength = headerData.GetType<6>();
        uint32_t const serial = headerData.GetType<5>();
        DBusMessageType const messageType = static_cast<DBusMessageType>(headerData.GetType<1>());

        // Now, read the rest of the message, this is the array length + padding + messageLength
        uint32_t size{FIRST_HEADER_PART_SIZE + headerFieldArrLength};
        uint32_t const nrOfPaddingBytes = AddPaddingToSize(size, DBUS_MESSAGE_BODY_ALIGNMENT);
        co_await boost::asio::async_read(*state->socket, boost::asio::dynamic_buffer(rawFullReply),
                                         boost::asio::transfer_exactly(size - FIRST_HEADER_PART_SIZE + messageLength),
                                         boost::asio::use_awaitable);

        // Skip over the padding, we don't care about it
        IncomingDBusMessage message{
            DBusMessageHeader{
                std::span<byte const>{rawFullReply.begin(),
                                      rawFullReply.begin() + FIRST_HEADER_PART_SIZE + headerFieldArrLength},
                serial, messageType, headerFieldArrLength, messageLength},
            std::ranges::to<std::vector>(
                rawFullReply | std::views::drop(FIRST_HEADER_PART_SIZE + headerFieldArrLength + nrOfPaddingBytes))};
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
}  // namespace cxxbus
