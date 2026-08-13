#include "DBusConnection.h"

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
        co_await boost::asio::async_read(*state->socket, boost::asio::buffer(tempBuffer),
                                         boost::asio::bind_executor(*state->strand, boost::asio::use_awaitable));
        // co_await boost::asio::async_read(*state->socket, boost::asio::buffer(tempBuffer),
        // boost::asio::use_awaitable);
#if __cpp_lib_containers_ranges
        rawFullReply.append_range(tempBuffer);
#else
        rawFullReply.insert(rawFullReply.end(), tempBuffer.begin(), tempBuffer.end());
#endif
        DBusMessageHeader messageHeader{std::move(tempBuffer)};

        tempBuffer.resize(sizeof(uint32_t));
        co_await boost::asio::async_read(*state->socket, boost::asio::buffer(tempBuffer),
                                         boost::asio::bind_executor(*state->strand, boost::asio::use_awaitable));
        // co_await boost::asio::async_read(*state->socket, boost::asio::buffer(tempBuffer),
        // boost::asio::use_awaitable);
#if __cpp_lib_containers_ranges
        rawFullReply.append_range(tempBuffer);
#else
        rawFullReply.insert(rawFullReply.end(), tempBuffer.begin(), tempBuffer.end());
#endif
        messageHeader.ParseHeaderFieldLength(std::move(tempBuffer));

        tempBuffer.resize(messageHeader.GetHeaderFieldsLength());
        co_await boost::asio::async_read(*state->socket, boost::asio::buffer(tempBuffer),
                                         boost::asio::bind_executor(*state->strand, boost::asio::use_awaitable));
        // co_await boost::asio::async_read(*state->socket, boost::asio::buffer(tempBuffer),
        // boost::asio::use_awaitable);
#if __cpp_lib_containers_ranges
        rawFullReply.append_range(std::move(tempBuffer));
#else
        rawFullReply.insert(rawFullReply.end(), tempBuffer.begin(), tempBuffer.end());
#endif

        uint32_t arrPointer{FIRST_HEADER_PART_SIZE};
        messageHeader.ParseRemainderOfHeader(rawFullReply, arrPointer);

        uint32_t const oldArrPointer{arrPointer};
        AddPaddingToSize(arrPointer, DBUS_MESSAGE_BODY_ALIGNMENT);
        uint32_t const nrOfPaddingBytes{arrPointer - oldArrPointer};

        tempBuffer.resize(nrOfPaddingBytes + messageHeader.GetMessageLength());
        co_await boost::asio::async_read(*state->socket, boost::asio::buffer(tempBuffer),
                                         boost::asio::bind_executor(*state->strand, boost::asio::use_awaitable));
        // co_await boost::asio::async_read(*state->socket, boost::asio::buffer(tempBuffer),
        // boost::asio::use_awaitable);

        // Skip over the padding, we don't care about it
#if __cpp_lib_ranges_to_container
        IncomingDBusMessage message{std::move(messageHeader),
                                    std::ranges::to<std::vector>(tempBuffer | std::views::drop(nrOfPaddingBytes))};
#else
        IncomingDBusMessage message{std::move(messageHeader),
                                    std::vector<byte>(tempBuffer.begin() + nrOfPaddingBytes, tempBuffer.end())};
#endif

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
        LOGGER.LogError("Read loop lost connection to dbus-daemon: {}", ex.what());
        boost::asio::co_spawn(*m_state->ioContext, HandleConnectionLost(), boost::asio::detached);
        break;
      }
      catch (std::exception const& ex)
      {
        LOGGER.LogError("Error occured in message read loop: {}", ex.what());
      }
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
        auto [message, serial, messageSentChannel] = co_await state->sendLoop.async_receive(
            boost::asio::bind_executor(*m_state->strand, boost::asio::use_awaitable));
        co_await boost::asio::async_write(*state->socket, boost::asio::buffer(message.Serialize(serial)),
                                          boost::asio::bind_executor(*state->strand, boost::asio::use_awaitable));

        std::string const info = message.GetInfo();
        LOGGER.LogTrace("Sent message '{}' with serial '{}'", info, serial);
        co_await messageSentChannel->async_send(
            boost::system::error_code{}, boost::asio::bind_executor(*m_state->strand, boost::asio::use_awaitable));
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
        LOGGER.LogError("Send loop lost connection to dbus-daemon: {}", ex.what());
        boost::asio::co_spawn(*m_state->ioContext, HandleConnectionLost(), boost::asio::detached);
        break;
      }
      catch (std::exception const& ex)
      {
        LOGGER.LogError("Error occured in message send loop: {}", ex.what());
      }
    }
  }

}  // namespace cxxbus
