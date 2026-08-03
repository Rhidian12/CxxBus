#include "SyncDBusConnection.h"

#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <memory>
#include <vector>

#include "DBusTypes.h"
#include "IncomingDBusMessage.h"
#include "Log.h"
#include "DBusConnection.h"
#include "magic_enum.hpp"

namespace cxxbus
{
  namespace
  {
#ifndef CXX_BUS_LOGLEVEL
#define CXX_BUS_LOGLEVEL Error
#endif  // CXX_BUS_LOGLEVEL

    Logger const LOGGER{.logLevel = LogLevel::CXX_BUS_LOGLEVEL};

    IncomingDBusMessage ReadMessageFromSocket(boost::asio::local::stream_protocol::socket& socket)
    {
      std::vector<byte> rawFullReply{};
      std::vector<byte> tempBuffer{};
      tempBuffer.resize(FIRST_HEADER_PART_SIZE);
      boost::asio::read(socket, boost::asio::buffer(tempBuffer));
#if  __cpp_lib_containers_ranges
      rawFullReply.append_range(tempBuffer);
#else
      rawFullReply.insert(rawFullReply.end(), tempBuffer.begin(), tempBuffer.end());
#endif
      DBusMessageHeader messageHeader{std::move(tempBuffer)};

      tempBuffer.resize(sizeof(uint32_t));
      boost::asio::read(socket, boost::asio::buffer(tempBuffer));
#if  __cpp_lib_containers_ranges
      rawFullReply.append_range(tempBuffer);
#else
      rawFullReply.insert(rawFullReply.end(), tempBuffer.begin(), tempBuffer.end());
#endif
      messageHeader.ParseHeaderFieldLength(std::move(tempBuffer));

      tempBuffer.resize(messageHeader.GetHeaderFieldsLength());
      boost::asio::read(socket, boost::asio::buffer(tempBuffer));
#if  __cpp_lib_containers_ranges
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
      boost::asio::read(socket, boost::asio::buffer(tempBuffer));

#if __cpp_lib_ranges_to_container
      // Skip over the padding, we don't care about it
      IncomingDBusMessage message{std::move(messageHeader),
                                  std::ranges::to<std::vector>(tempBuffer | std::views::drop(nrOfPaddingBytes))};
#else
      // Skip over the padding, we don't care about it
      IncomingDBusMessage message{std::move(messageHeader),
                                  std::vector<byte>(tempBuffer.begin() + nrOfPaddingBytes, tempBuffer.end())};
#endif

      LOGGER.LogTrace(std::format(
          "Received incoming message with serial '{}' and signature '{}' and member '{}' and type '{}'",
          message.GetHeader().GetSerial(), std::string{message.GetHeader().GetSignature().value_or(Signature{""})},
          message.GetHeader().GetMember().value_or(""), magic_enum::enum_name(message.GetHeader().GetMessageType())));

      return message;
    }
  }  // namespace

  SyncDBusConnection::SyncDBusConnection(DBusConnection& dbusConnection)
    : m_state(new InternalState{.socket = dbusConnection.m_state->socket,
      .uniqueConnection = dbusConnection.m_state->uniqueConnection,
      .wellKnownName = dbusConnection.m_state->wellKnownName,
                                .serial = dbusConnection.m_state->serial, // Make sure we can't overlap serials
                                .subscriptionCounter = dbusConnection.m_state->subscriptionCounter,
                                .matchRules = dbusConnection.m_state->matchRules,
                                .nameCache = dbusConnection.m_state->nameCache,
                                .unhandledIncomingMessages = dbusConnection.m_state->unhandledIncomingMessages})
  {
    // Don't connect here, we're coming from a 'DBusConnection' which should've already done all that stuff
  }

  std::shared_ptr<SyncDBusConnection> SyncDBusConnection::Create(DBusConnection& dbusConnection)
  {
    return std::shared_ptr<SyncDBusConnection>{new SyncDBusConnection{dbusConnection}};
  }

  void SyncDBusConnection::AddMatchRule(DBusMatchRule rule,  std::function<boost::asio::awaitable<void>(IncomingDBusMessage)> callback)
  {
    LOGGER.LogTrace(std::format("Adding match rule '{}'", rule.GetRule()));

    SendMessage(DBusMessage::Method("AddMatch")
                    .Path(ObjectPath{"/org/freedesktop/DBus"})
                    .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                    .Destination("org.freedesktop.DBus")
                    .Parameter(rule.GetRule()));

    std::shared_ptr<AwaitableSignal<void, IncomingDBusMessage>> signal =
        std::make_shared<AwaitableSignal<void, IncomingDBusMessage>>();
    signal->connect(
        [cb = std::move(callback)](IncomingDBusMessage message) -> std::function<boost::asio::awaitable<void>()>
        {
          return [cb, message = std::move(message)](this auto&&) -> boost::asio::awaitable<void>
          // This cannot be a lambda because the lambda would get destroyed, causing `cb` and `message` to go
          // out-of-scope
          { return InvokeAsyncCallback(cb, std::move(message)); };
        });

    m_state->matchRules->emplace((*m_state->subscriptionCounter)++,
                                DBusConnection::MatchRuleInfo{.rule = std::move(rule), .callback = std::move(signal)});
  }

  void SyncDBusConnection::RemoveMatchRule(DBusMatchRule rule)
  {
    SendMessage(DBusMessage::Method("RemoveMatch")
                    .Path(ObjectPath{"/org/freedesktop/DBus"})
                    .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                    .Destination("org.freedesktop.DBus")
                    .Parameter(rule.GetRule()));

    auto const it = std::ranges::find_if(*m_state->matchRules, [&rule](std::pair<uint32_t, DBusConnection::MatchRuleInfo> const& elem)
                                         { return elem.second.rule == rule; });
    m_state->matchRules->erase(it);
  }

  bool SyncDBusConnection::HandleReadMessage(IncomingDBusMessage message, uint32_t expectedReplySerial)
  {
    if (message.GetHeader().GetReplySerial().has_value() &&
        message.GetHeader().GetReplySerial().value() == expectedReplySerial)
    {
      LOGGER.LogTrace(
          std::format("Message is a reply to serial '{}'", message.GetHeader().GetReplySerial().value()));

      if (message.GetHeader().GetMessageType() == DBusMessageType::ERROR)
      {
        // We got an error, so throw an error here
        if (!message.GetHeader().GetErrorName().has_value())
        {
          LOGGER.LogFatal("Incoming DBus Error did not specify the ERROR_NAME header field");
        }

        throw DBusError{
            message.GetHeader().GetErrorName().has_value() ? message.GetHeader().GetErrorName().value() : "Missing",
            message.HasArguments() && message.GetHeader().GetSignature().value_or(Signature{""}) == "s"
                ? message.Get<std::string>()
                : "No error message was provided by the remote"};
      }

      return true;
    }
    else
    {
      LOGGER.LogTrace("Got incoming message that is not a reply. Adding it to queue");

      m_state->unhandledIncomingMessages->push(std::move(message));
    }

    return false;
  }

  IncomingDBusMessage SyncDBusConnection::SendMessage(DBusMessage message)
  {
    uint32_t const serial{(*m_state->serial)++};

    boost::asio::write(*m_state->socket, boost::asio::buffer(message.Serialize(serial)));
    LOGGER.LogTrace(std::format(
        "Sent message with method '{}' and serial '{}' to path '{}' with interface '{}'",
        message.GetMember().value_or(""), serial,
        message.GetPath().transform([](ObjectPath const& p) { return p.GetPath(); }).value_or(""),
        message.GetInterface().transform([](DBusInterfaceName const& i) { return i.GetName(); }).value_or("")));

    while (true)
    {
      IncomingDBusMessage const message{ReadMessageFromSocket(*m_state->socket)};
      if (HandleReadMessage(message, serial))
      {
        return message;
      }
    }
  }

  void SyncDBusConnection::SendMessageNoReply(DBusMessage message)
  {
    // Let's auto add the NO_REPLY_EXPECTED flag if it's not been added
    if (!std::ranges::contains(message.GetFlags(), DBusMessageFlags::NO_REPLY_EXPECTED))
    {
      message.Flag(DBusMessageFlags::NO_REPLY_EXPECTED);
    }

    boost::asio::write(*m_state->socket, boost::asio::buffer(message.Serialize((*m_state->serial)++)));
    LOGGER.LogTrace(std::format(
        "Sent message with method '{}' and serial '{}' to path '{}' with interface '{}'",
        message.GetMember().value_or(""), *m_state->serial,
        message.GetPath().transform([](ObjectPath const& p) { return p.GetPath(); }).value_or(""),
        message.GetInterface().transform([](DBusInterfaceName const& i) { return i.GetName(); }).value_or("")));
  }

  DBusWellKnownName const& SyncDBusConnection::GetWellKnownName() const
  {
    return *m_state->wellKnownName;
  }
}  // namespace cxxbus
