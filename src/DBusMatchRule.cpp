#include "DBusMatchRule.h"

#include <cstdint>
#include <format>
#include <string>

#include "DBusTypes.h"
#include "IncomingDBusMessage.h"
#include "Log.h"

namespace cxxbus
{
  namespace
  {
    constexpr char const* MESSAGE_TYPE_STRINGS[] = {"method_call", "method_return", "error", "signal"};

#ifndef CXX_BUS_LOGLEVEL
#define CXX_BUS_LOGLEVEL Error
#endif  // CXX_BUS_LOGLEVEL

    Logger const LOGGER{.logLevel = LogLevel::CXX_BUS_LOGLEVEL};
  }  // namespace

  DBusMatchRule DBusMatchRule::Create()
  {
    return DBusMatchRule{};
  }

  DBusMatchRule& DBusMatchRule::Type(DBusMessageType messageType)
  {
    switch (messageType)
    {
      case DBusMessageType::INVALID:
      case DBusMessageType::NONE:
      case DBusMessageType::OPTIONAL:
        LOGGER.LogError("INVALID, NONE and OPTIONAL are invalid message types to create a match rule on");
        throw InvalidDBusMatchRule{std::format("INVALID, NONE and OPTIONAL are invalid message types to create a match rule on")};
      default:
        break;
    }

    m_messageType = messageType;

    return *this;
  }

  DBusMatchRule& DBusMatchRule::Sender(std::variant<DBusWellKnownName, DBusUniqueConnectionName> senderName)
  {
    m_sender = std::visit([](auto&& name) { return std::string{name}; }, std::move(senderName));

    return *this;
  }

  DBusMatchRule& DBusMatchRule::Interface(std::string interface)
  {
    m_interface = std::move(interface);

    return *this;
  }

  DBusMatchRule& DBusMatchRule::Member(std::string member)
  {
    m_member = std::move(member);

    return *this;
  }

  DBusMatchRule& DBusMatchRule::Path(ObjectPath path)
  {
    if (m_pathNamespace.has_value())
    {
      throw InvalidDBusMatchRule{"It is not allowed for a match rule to contain both 'Path' and 'PathNamespace'"};
    }

    m_path = std::move(path);

    return *this;
  }

  DBusMatchRule& DBusMatchRule::PathNamespace(ObjectPath path)
  {
    if (m_path.has_value())
    {
      throw InvalidDBusMatchRule{"It is not allowed for a match rule to contain both 'Path' and 'PathNamespace'"};
    }

    m_pathNamespace = std::move(path);

    return *this;
  }

  DBusMatchRule& DBusMatchRule::Destination(DBusUniqueConnectionName destination)
  {
    m_destination = std::move(destination);

    return *this;
  }

  DBusMatchRule& DBusMatchRule::Argument(uint8_t index, std::string member)
  {
    if (index > 63)
    {
      throw InvalidDBusMatchRule{std::format("DBus Match rules can only match on arguments with a maximum index of 63. Provided index: {}", index)};
    }

    m_args.emplace_back(member, index);
    return *this;
  }

  DBusMatchRule& DBusMatchRule::ArgumentPath(uint8_t index, std::string member)
  {
    if (index > 63)
    {
      throw InvalidDBusMatchRule{std::format("DBus Match rules can only match on arguments with a maximum index of 63. Provided index: {}", index)};
    }

    m_argPaths.emplace_back(member, index);
    return *this;
  }

  DBusMatchRule& DBusMatchRule::ArgumentNamespace(std::variant<DBusWellKnownName, DBusUniqueConnectionName, std::string> name)
  {
    m_argNamespace = std::visit([](auto&& arg) { return std::string{arg}; }, std::move(name));

    return *this;
  }

  DBusMatchRule& DBusMatchRule::EavesDrop(bool eavesdrop)
  {
    m_eavesdrop = eavesdrop;

    return *this;
  }

#ifndef CXX_BUS_ADD_TO_RULE
#define CXX_BUS_ADD_TO_RULE(str, key, value) \
  if (!(str).empty()) (str).push_back(',');  \
  (str) += std::format("{}='{}'", (key), (value));
#endif  // CXX_BUS_ADD_TO_RULE

#ifndef CXX_BUS_ADD_TO_RULE_CHECK_OPTIONAL
#define CXX_BUS_ADD_TO_RULE_CHECK_OPTIONAL(str, member, key, value) \
  if ((member).has_value())                                         \
  {                                                                 \
    CXX_BUS_ADD_TO_RULE(str, key, value)                            \
  }
#endif  // CXX_BUS_ADD_TO_RULE_CHECK_OPTIONAL

  std::string DBusMatchRule::GetRule() const
  {
    std::string rule;
    CXX_BUS_ADD_TO_RULE_CHECK_OPTIONAL(rule, m_messageType, "type", MESSAGE_TYPE_STRINGS[static_cast<uint8_t>(*m_messageType) - 1])
    CXX_BUS_ADD_TO_RULE_CHECK_OPTIONAL(rule, m_sender, "sender", *m_sender)
    CXX_BUS_ADD_TO_RULE_CHECK_OPTIONAL(rule, m_interface, "interface", *m_interface)
    CXX_BUS_ADD_TO_RULE_CHECK_OPTIONAL(rule, m_member, "member", *m_member)
    CXX_BUS_ADD_TO_RULE_CHECK_OPTIONAL(rule, m_path, "path", std::string{*m_path})
    CXX_BUS_ADD_TO_RULE_CHECK_OPTIONAL(rule, m_pathNamespace, "path_namespace", std::string{*m_pathNamespace})
    CXX_BUS_ADD_TO_RULE_CHECK_OPTIONAL(rule, m_destination, "destination", std::string{*m_destination})
    for (ArgInfo const& argInfo : m_args)
    {
      CXX_BUS_ADD_TO_RULE(rule, std::format("arg{}", argInfo.index), argInfo.name);
    }
    for (ArgInfo const& argInfo : m_argPaths)
    {
      CXX_BUS_ADD_TO_RULE(rule, std::format("arg{}path", argInfo.index), argInfo.name);
    }
    CXX_BUS_ADD_TO_RULE_CHECK_OPTIONAL(rule, m_argNamespace, "arg0namespace", *m_argNamespace)
    CXX_BUS_ADD_TO_RULE_CHECK_OPTIONAL(rule, m_eavesdrop, "eavesdrop", (*m_eavesdrop) ? "true" : "false")

    if (rule.empty())
    {
      throw EmptyDBusMatchRule{"Can't add an empty match rule"};
    }

    return rule;
  }

#ifndef CXX_BUS_CHECK_MATCH
#define CXX_BUS_CHECK_MATCH(res, expr, var) res &= ((expr) == (var));
#endif  // CXX_BUS_CHECK_MATCH

#ifndef CXX_BUS_CHECK_MATCH_OPTIONAL
#define CXX_BUS_CHECK_MATCH_OPTIONAL(res, expr, var) \
  if (res && (var).has_value())                      \
  {                                                  \
    CXX_BUS_CHECK_MATCH(res, expr, var)              \
  }
#endif  // CXX_BUS_CHECK_MATCH_OPTIONAL

  bool DBusMatchRule::Matches(IncomingDBusMessage const& message, std::vector<std::string> const& wellKnownNames) const
  {
    DBusMessageHeader const& header{message.GetHeader()};
    bool matches{true};

    CXX_BUS_CHECK_MATCH_OPTIONAL(matches, header.GetMessageType(), m_messageType)
    CXX_BUS_CHECK_MATCH_OPTIONAL(matches, header.GetMember(), m_member)

    auto const& sender = header.GetSender();
    CXX_BUS_CHECK_MATCH_OPTIONAL(matches, sender, m_sender)
    for (std::string const& wellKnownName : wellKnownNames)
    {
      CXX_BUS_CHECK_MATCH(matches, sender, wellKnownName)
    }

    CXX_BUS_CHECK_MATCH_OPTIONAL(matches, header.GetInterface(), m_interface)
    CXX_BUS_CHECK_MATCH_OPTIONAL(matches, header.GetObjectPath(), m_path)
    CXX_BUS_CHECK_MATCH_OPTIONAL(matches, header.GetDestination(), m_destination.transform([](DBusUniqueConnectionName const& name) { return name.GetName(); }))

    // [TODO]: Add argument & path namespace & argument paths & arg namespace & eavesdrop matching

    return matches;
  }
}  // namespace cxxbus