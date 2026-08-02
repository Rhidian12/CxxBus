#include "SyncDBusConnection.h"

#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <memory>
#include <vector>

#include "DBusTypes.h"
#include "IncomingDBusMessage.h"
#include "Log.h"
#include "magic_enum.hpp"

namespace cxxbus
{
  namespace
  {
#ifndef CXX_BUS_LOGLEVEL
#define CXX_BUS_LOGLEVEL Error
#endif  // CXX_BUS_LOGLEVEL

    IncomingDBusMessage ReadMessageFromSocket(boost::asio::local::stream_protocol::socket& socket, Logger logger)
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

      logger.LogTrace(std::format(
          "Received incoming message with serial '{}' and signature '{}' and member '{}' and type '{}'",
          message.GetHeader().GetSerial(), std::string{message.GetHeader().GetSignature().value_or(Signature{""})},
          message.GetHeader().GetMember().value_or(""), magic_enum::enum_name(message.GetHeader().GetMessageType())));

      return message;
    }

    void AuthenticateDBusConnection(boost::asio::local::stream_protocol::socket& socket, Logger logger)
    {
      // First send a single '\0' byte
      socket.send(boost::asio::buffer("\0", 1));

      // Next we must authenticate ourselves, we use the EXTERNAL
      // authentication method
      socket.send(
          boost::asio::buffer(std::format("AUTH EXTERNAL {}\r\n", HexEncodeString(std::to_string(::getuid())))));

      // Now we expect to see OK <guid>
      std::string reply{};
      boost::asio::read_until(socket, boost::asio::dynamic_buffer(reply), "\r\n");

      if (!reply.starts_with("OK"))
      {
        logger.LogError("Authentication failed!");
        throw std::runtime_error{"Authentication failed!"};
      }

      // Yippee! All worked, so now start our DBus Connection!
      socket.send(boost::asio::buffer("BEGIN\r\n", 7));
    }
  }  // namespace

  SyncDBusConnection::SyncDBusConnection(boost::asio::io_context& ioContext, DBusWellKnownName wellKnownName)
    : m_state(new InternalState{.socket = boost::asio::local::stream_protocol::socket{ioContext},
                                .objectPathHandlers = {},
                                .onIncomingSignal = {},
                                .serial = 1,
                                .uniqueConnection = "",
                                .wellKnownName = std::move(wellKnownName),
                                .subscriptionCounter = 0,
                                .matchRules = {},
                                .nameCache = {*this},
                                .messagesToDispatch = {},
                                .dispatchHandler = {},
                                .pollHandler = {},
                                .logger = {.logLevel = LogLevel::CXX_BUS_LOGLEVEL}})
  {
    Connect();
  }

  SyncDBusConnection::~SyncDBusConnection()
  {
    if (m_state->socket.is_open())
    {
      Close();
    }
  }

  void SyncDBusConnection::CloseData()
  {
    m_state->logger.LogTrace("Closing sockets, channels and signals");
    if (m_state->socket.is_open())
    {
      boost::system::error_code ec;
      std::ignore = m_state->socket.close(ec);
    }
  }

  void SyncDBusConnection::Close()
  {
    m_state->logger.LogInfo("Closing DBus Connection");

    std::unordered_map<uint32_t, MatchRuleInfo> rules{m_state->matchRules};
    for (auto const& [id, ruleInfo] : rules)
    {
      RemoveMatchRule(ruleInfo.rule);
    }

    // Release our well-known name from the dbus-daemon
    IncomingDBusMessage const ret = SendMessage(DBusMessage::Method("ReleaseName")
                                                    .Path(ObjectPath{"/org/freedesktop/DBus"})
                                                    .Destination("org.freedesktop.DBus")
                                                    .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                                    .Parameter(std::string{m_state->wellKnownName}));

    switch (ret.Get<uint32_t>())
    {
      case 1:
        m_state->logger.LogDebug(
            std::format("Successfully released well-known name '{}'", m_state->wellKnownName.GetName()));
        break;
      case 2:
        m_state->logger.LogError(
            std::format("Well-known name '{}' is not owned by the dbus-daemon", m_state->wellKnownName.GetName()));
        break;
      case 3:
        m_state->logger.LogError(
            std::format("Well-known name '{}' is not owned by this connection", m_state->wellKnownName.GetName()));
        break;
      default:
        m_state->logger.LogError(std::format("Unknown return value from 'ReleaseName()': {}", ret.Get<uint32_t>()));
        break;
    }

    CloseData();
  }

  std::shared_ptr<SyncDBusConnection> SyncDBusConnection::Create(boost::asio::io_context& ioContext,
                                                                 DBusWellKnownName wellKnownName)
  {
    return std::shared_ptr<SyncDBusConnection>{new SyncDBusConnection{ioContext, std::move(wellKnownName)}};
  }

  void SyncDBusConnection::Connect()
  {
    // Connect to DBus daemon
    boost::asio::local::stream_protocol::endpoint endpoint{ParseDBusAddress()};
    m_state->socket.connect(endpoint);

    AuthenticateDBusConnection(m_state->socket, m_state->logger);
    m_state->logger.LogTrace("Connected to DBus-daemon.");

    m_state->logger.LogTrace("Starting connection handshake");
    // Get our unique bus name
    IncomingDBusMessage reply = SendMessage(DBusMessage::Method("Hello")
                                                .Path(ObjectPath{"/org/freedesktop/DBus"})
                                                .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                                                .Destination("org.freedesktop.DBus"));
    m_state->uniqueConnection = reply.Get<std::string>();

    m_state->logger.LogInfo(std::format("Unique Connection ID: {}", m_state->uniqueConnection));

    // Now, request a well-known name from the dbus-daemon
    reply = SendMessage(DBusMessage::Method("RequestName")
                            .Path(ObjectPath{"/org/freedesktop/DBus"})
                            .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                            .Destination("org.freedesktop.DBus")
                            .Parameter(MultipleCompleteTypes<std::string, uint32_t>{m_state->wellKnownName.GetName(),
                                                                                    static_cast<uint32_t>(0x1)}));

    switch (reply.Get<uint32_t>())
    {
      case 1:
        m_state->logger.LogDebug(
            std::format("Successfully acquired well-known name '{}'", m_state->wellKnownName.GetName()));
        break;
      // [TODO]: Allow user passing flags for the Well-known name.
      case 2:
        m_state->logger.LogError(
            std::format("Well-known name '{}' is already owned by another connection and we did "
                        "not ask to replace the name",
                        m_state->wellKnownName.GetName()));
        break;
      case 3:
        m_state->logger.LogError(
            std::format("The well-known name '{}' already has an owner", m_state->wellKnownName.GetName()));
        break;
      case 4:
        m_state->logger.LogDebug("We're already owner of our well-known name");
        break;
      default:
        m_state->logger.LogError(std::format("Unknown return value from 'RequestName()': {}", reply.Get<uint32_t>()));
        break;
    }

    m_state->logger.LogTrace("Connection handshake completed.");

    m_state->logger.LogTrace("Subscribing to NameOwnerChanged signal");
    m_state->nameCache.SubscribeToNameChangesSync();
  }

  void SyncDBusConnection::RegisterObjectPathHandler(ObjectPath path,
                                                     std::function<void(IncomingDBusMessage const&)> callback)
  {
    m_state->objectPathHandlers[path.GetPath()].connect(std::move(callback));
  }

  void SyncDBusConnection::ReceiveIncomingMessages(std::function<void(IncomingDBusMessage const&)> callback)
  {
    m_state->onIncomingSignal.connect(std::move(callback));
  }

  void SyncDBusConnection::AddMatchRule(DBusMatchRule rule, std::function<void(IncomingDBusMessage const&)> callback)
  {
    m_state->logger.LogTrace(std::format("Adding match rule '{}'", rule.GetRule()));

    SendMessage(DBusMessage::Method("AddMatch")
                    .Path(ObjectPath{"/org/freedesktop/DBus"})
                    .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                    .Destination("org.freedesktop.DBus")
                    .Parameter(rule.GetRule()));

    m_state->matchRules.emplace(m_state->subscriptionCounter++,
                                MatchRuleInfo{.rule = std::move(rule), .callback = std::move(callback)});
  }

  void SyncDBusConnection::RemoveMatchRule(DBusMatchRule rule)
  {
    SendMessage(DBusMessage::Method("RemoveMatch")
                    .Path(ObjectPath{"/org/freedesktop/DBus"})
                    .Interface(DBusInterfaceName{"org.freedesktop.DBus"})
                    .Destination("org.freedesktop.DBus")
                    .Parameter(rule.GetRule()));

    auto const it = std::ranges::find_if(m_state->matchRules, [&rule](std::pair<uint32_t, MatchRuleInfo> const& elem)
                                         { return elem.second.rule == rule; });
    m_state->matchRules.erase(it);
  }

  bool SyncDBusConnection::HandleReadMessage(IncomingDBusMessage message)
  {
    if (message.GetHeader().GetReplySerial().has_value() &&
        message.GetHeader().GetReplySerial().value() == m_state->serial - 1)
    {
      m_state->logger.LogTrace(
          std::format("Message is a reply to serial '{}'", message.GetHeader().GetReplySerial().value()));

      if (message.GetHeader().GetMessageType() == DBusMessageType::ERROR)
      {
        // We got an error, so throw an error here
        if (!message.GetHeader().GetErrorName().has_value())
        {
          m_state->logger.LogFatal("Incoming DBus Error did not specify the ERROR_NAME header field");
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
      m_state->logger.LogTrace("New message added to dispatch queue");

      m_state->messagesToDispatch.push(std::move(message));

      if (m_state->dispatchHandler)
      {
        m_state->dispatchHandler(DispatchStatus::DISPATCH_PENDING);
      }
    }

    return false;
  }

  IncomingDBusMessage SyncDBusConnection::SendMessage(DBusMessage message)
  {
    boost::asio::write(m_state->socket, boost::asio::buffer(message.Serialize(m_state->serial++)));
    m_state->logger.LogTrace(std::format(
        "Sent message with method '{}' and serial '{}' to path '{}' with interface '{}'",
        message.GetMember().value_or(""), m_state->serial - 1,
        message.GetPath().transform([](ObjectPath const& p) { return p.GetPath(); }).value_or(""),
        message.GetInterface().transform([](DBusInterfaceName const& i) { return i.GetName(); }).value_or("")));

    while (true)
    {
      IncomingDBusMessage const message{ReadMessageFromSocket(m_state->socket, m_state->logger)};
      if (HandleReadMessage(message))
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

    boost::asio::write(m_state->socket, boost::asio::buffer(message.Serialize(m_state->serial++)));
    m_state->logger.LogTrace(std::format(
        "Sent message with method '{}' and serial '{}' to path '{}' with interface '{}'",
        message.GetMember().value_or(""), m_state->serial - 1,
        message.GetPath().transform([](ObjectPath const& p) { return p.GetPath(); }).value_or(""),
        message.GetInterface().transform([](DBusInterfaceName const& i) { return i.GetName(); }).value_or("")));
  }

  void SyncDBusConnection::SetDispatchHandler(std::function<void(DispatchStatus)> callback)
  {
    m_state->dispatchHandler = std::move(callback);
  }

  DispatchStatus SyncDBusConnection::DispatchIncomingMessages()
  {
    if (m_state->messagesToDispatch.empty())
    {
      m_state->logger.LogTrace("No more messages to dispatch");
      return DispatchStatus::COMPLETED;
    }

    IncomingDBusMessage message = m_state->messagesToDispatch.front();
    m_state->messagesToDispatch.pop();

    m_state->logger.LogTrace(
        std::format("Dispatching queued message with serial '{}', method '{}', interface '{}' and path '{}'",
                    message.GetHeader().GetSerial(), message.GetHeader().GetMember().value_or(""),
                    message.GetHeader()
                        .GetInterface()
                        .transform([](DBusInterfaceName const& name) { return name.GetName(); })
                        .value_or(""),
                    message.GetHeader()
                        .GetObjectPath()
                        .transform([](ObjectPath const& path) { return path.GetPath(); })
                        .value_or("")));

    if (message.GetHeader().GetMessageType() == DBusMessageType::SIGNAL)
    {
      m_state->logger.LogTrace("Incoming message is signal, checking match rules");

      for (MatchRuleInfo const& info : m_state->matchRules | std::views::values)
      {
        std::vector<std::string> wellKnownNames =
            m_state->nameCache.GetWellKnownNames(message.GetHeader().GetSender().value_or(""));
        if (info.rule.Matches(message, wellKnownNames))
        {
          m_state->logger.LogTrace(std::format("Rule '{}' matched incoming signal", info.rule.GetRule()));
          info.callback(message);
        }
      }
    }
    else if (message.GetHeader().GetObjectPath().has_value() &&
             m_state->objectPathHandlers.contains(message.GetHeader().GetObjectPath()->GetPath()))
    {
      m_state->logger.LogTrace("Message's object path has a registered handler. Calling the handler");
      m_state->objectPathHandlers[message.GetHeader().GetObjectPath()->GetPath()](message);
    }
    else
    {
      m_state->logger.LogTrace(
          "Falling back to general message handler as message did not match rules or object paths");
      m_state->logger.LogTrace(std::format("Do we have handlers defined? {}", !m_state->onIncomingSignal.empty()));
      m_state->onIncomingSignal(message);
    }

    return DispatchStatus::DISPATCH_PENDING;
  }

  void SyncDBusConnection::SetPollHandler(std::function<void(boost::asio::local::stream_protocol::socket&)> callback)
  {
    m_state->logger.LogTrace("Starting poll handler");
    m_state->pollHandler = std::move(callback);
    m_state->pollHandler(m_state->socket);
  }

  void SyncDBusConnection::Poll()
  {
    m_state->logger.LogTrace("Polling message from socket");
    HandleReadMessage(ReadMessageFromSocket(m_state->socket, m_state->logger));
  }

  DBusWellKnownName const& SyncDBusConnection::GetWellKnownName() const
  {
    return m_state->wellKnownName;
  }

  void SyncDBusConnection::SetLogLevel(LogLevel logLevel)
  {
    m_state->logger.logLevel = logLevel;
  }
}  // namespace cxxbus
