// MIT License
//
// Copyright (c) 2026 Rhidian De Wit
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "DBusMatchRule.h"

#include <sys/types.h>

#include <cstdint>
#include <format>
#include <sstream>
#include <string>
#include <unordered_map>

#include "DBus.h"
#include "DBusHelpers.h"
#include "DBusTypes.h"
#include "IncomingDBusMessage.h"
#include "Log.h"

namespace cxxbus
{
  namespace
  {
    constexpr char const* MESSAGE_TYPE_STRINGS[] = {"method_call", "method_return", "error", "signal"};

    void WalkSignature(Signature const& signature, std::vector<byte> const& messageBody, uint32_t& pointer,
                       uint8_t& argIndex, std::unordered_map<uint8_t, std::string>& stringArgs,
                       std::unordered_map<uint8_t, ObjectPath>& objectPathArgs, bool allowedToAddArgs = true)
    {
      for (size_t i{}; i < signature.size(); ++i)
      {
        char const c = signature.GetSignature()[i];

        SkipPadding(pointer, GetAlignmentOfSignature(c));

        if (IsDBusBasicFixedTypeCode(c))
        {
          pointer += GetAlignmentOfSignature(c);
        }
        else if (IsDBusBasicStringlikeTypeCode(c))
        {
          switch (static_cast<DBusTypeCodes>(c))
          {
            case DBusTypeCodes::STRING:
            {
              uint32_t strSize{UnmarshalDBusType<uint32_t>(
                  std::span<byte const>{messageBody.begin() + pointer, sizeof(uint32_t)}, "u")};
              if (allowedToAddArgs)
              {
                stringArgs.insert(std::make_pair(
                    argIndex,
                    UnmarshalDBusType<std::string>(
                        std::span<byte const>{messageBody.begin() + pointer, strSize + sizeof(uint32_t) + 1}, "s")));
              }
              pointer += strSize + sizeof(uint32_t) + 1;
            }
            break;
            case DBusTypeCodes::OBJECT_PATH:
            {
              uint32_t strSize{UnmarshalDBusType<uint32_t>(
                  std::span<byte const>{messageBody.begin() + pointer, sizeof(uint32_t)}, "u")};
              if (allowedToAddArgs)
              {
                objectPathArgs.insert(std::make_pair(
                    argIndex,
                    UnmarshalDBusType<std::string>(
                        std::span<byte const>{messageBody.begin() + pointer, strSize + sizeof(uint32_t) + 1}, "s")));
              }
              pointer += strSize + sizeof(uint32_t) + 1;
            }
            break;
            case DBusTypeCodes::SIGNATURE:
              pointer += UnmarshalDBusType<uint8_t>(
                             std::span<byte const>{messageBody.begin() + pointer, sizeof(uint8_t)}, "y") +
                         sizeof(uint8_t) + 1;
              break;
            default:
              LOG_FATAL(LOGGER,
                        "DBusMatchRule > While walking signature we encounter a non-string type where we really "
                        "expected one");
              throw InternalError{
                  "DBusMatchRule > While walking signature we encounter a non-string type where we really expected "
                  "one"};
              break;
          }
        }
        else
        {
          switch (static_cast<DBusTypeCodes>(c))
          {
            case DBusTypeCodes::ARRAY:
            {
              if (static_cast<DBusTypeCodes>(signature.GetSignature()[i + 1]) == DBusTypeCodes::DICT_BEGIN)
              {
                break;
              }

              uint32_t arrSize{UnmarshalDBusType<uint32_t>(
                  std::span<byte const>{messageBody.begin() + pointer, sizeof(uint32_t)}, "u")};
              pointer += sizeof(uint32_t);
              AddPaddingToSize(pointer, GetAlignmentOfSignature(signature.GetSignature()[i + 1]));
              pointer += arrSize;

              ++i;  // Skip array element
            }
            break;
            case DBusTypeCodes::DICT_BEGIN:
            {
              uint32_t dictSize{UnmarshalDBusType<uint32_t>(
                  std::span<byte const>{messageBody.begin() + pointer, sizeof(uint32_t)}, "u")};
              pointer += sizeof(uint32_t);
              SkipPadding(pointer, GetAlignmentOfSignature(c));
              pointer += dictSize;

              uint32_t bracketCounter = 1;
              while (bracketCounter > 0)
              {
                DBusTypeCodes const code{static_cast<DBusTypeCodes>(signature.GetSignature()[++i])};
                if (code == DBusTypeCodes::DICT_END)
                {
                  --bracketCounter;
                }
                else if (code == DBusTypeCodes::DICT_BEGIN)
                {
                  ++bracketCounter;
                }
              }
            }
            break;
            case DBusTypeCodes::STRUCT_BEGIN:
            {
              // go through each element separately but skip all of them
              DBusTypeCodes code{static_cast<DBusTypeCodes>(signature.GetSignature()[++i])};
              uint8_t currArgIndex{argIndex};
              while (code != DBusTypeCodes::STRUCT_END)
              {
                WalkSignature(Signature{std::string{signature.GetSignature()[i]}}, messageBody, pointer, argIndex,
                              stringArgs, objectPathArgs, false);
                code = static_cast<DBusTypeCodes>(signature.GetSignature()[++i]);
                if (static_cast<DBusTypeCodes>(signature.GetSignature()[i + 1]) == DBusTypeCodes::STRUCT_END)
                {
                  break;
                }
                SkipPadding(pointer, GetAlignmentOfSignature(signature.GetSignature()[i + 1]));
              }
              ++i;                      // Skip the closing struct bracket
              argIndex = currArgIndex;  // Make sure we didn't change the argIndex
            }
            break;
            case DBusTypeCodes::VARIANT:
            {
              uint8_t sigLength{UnmarshalDBusType<uint8_t>(
                  std::span<byte const>{messageBody.begin() + pointer, sizeof(uint8_t)}, "y")};
              Signature const varSig{UnmarshalDBusType<Signature>(
                  std::span<byte const>{messageBody.begin() + pointer + sizeof(uint8_t), sigLength}, "g")};
              pointer += sigLength + 1;
              SkipPadding(pointer, GetAlignmentOfSignature(varSig.GetSignature()[0]));

              uint8_t currArgIndex{argIndex};
              WalkSignature(varSig, messageBody, pointer, argIndex, stringArgs, objectPathArgs, false);
              argIndex = currArgIndex;  // Make sure we didn't change the argIndex
            }
            break;
            default:
              LOG_FATAL(LOGGER,
                        "DBusMatchRule > While walking signature we encounter a non-DBus-container type where we "
                        "really expected one");
              throw InternalError{
                  "DBusMatchRule > While walking signature we encounter a non-DBus-container type where we really "
                  "expected one"};
              break;
          }
        }

        ++argIndex;
      }
    }

    std::vector<std::string> SplitString(std::string const& s, char delimiter)
    {
      std::vector<std::string> res;
      std::stringstream ss{s};
      std::string temp;

      while (std::getline(ss, temp, delimiter))
      {
        res.push_back(temp);
        if (!ss.eof())
        {
          res.push_back(std::string{delimiter});
        }
      }

      auto it = std::ranges::remove(res, "");
      res.erase(it.begin(), it.end());

      return res;
    }

    bool IsPathPrefixed(std::vector<std::string> const& rule, std::vector<std::string> const& toCheck)
    {
      for (size_t i{}; i < rule.size(); ++i)
      {
        if (i >= toCheck.size() || toCheck[i] != rule[i])
        {
          return false;
        }
      }

      return true;
    }
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
        LOG_ERROR(LOGGER, "INVALID, NONE and OPTIONAL are invalid message types to create a match rule on");
        throw InvalidDBusMatchRule{
            std::format("INVALID, NONE and OPTIONAL are invalid message types to create a match rule on")};
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

  DBusMatchRule& DBusMatchRule::Interface(DBusInterfaceName interface)
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
      throw InvalidDBusMatchRule{std::format(
          "DBus Match rules can only match on arguments with a maximum index of 63. Provided index: {}", index)};
    }

    m_args.emplace_back(member, index);
    return *this;
  }

  DBusMatchRule& DBusMatchRule::ArgumentPath(uint8_t index, std::string member)
  {
    if (index > 63)
    {
      throw InvalidDBusMatchRule{std::format(
          "DBus Match rules can only match on arguments with a maximum index of 63. Provided index: {}", index)};
    }

    m_argPaths.emplace_back(member, index);
    return *this;
  }

  DBusMatchRule& DBusMatchRule::ArgumentNamespace(
      std::variant<DBusWellKnownName, DBusUniqueConnectionName, std::string> name)
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
    CXX_BUS_ADD_TO_RULE_CHECK_OPTIONAL(rule, m_messageType, "type",
                                       MESSAGE_TYPE_STRINGS[static_cast<uint8_t>(*m_messageType) - 1])
    CXX_BUS_ADD_TO_RULE_CHECK_OPTIONAL(rule, m_sender, "sender", *m_sender)
    CXX_BUS_ADD_TO_RULE_CHECK_OPTIONAL(rule, m_interface, "interface", m_interface->GetName())
    CXX_BUS_ADD_TO_RULE_CHECK_OPTIONAL(rule, m_member, "member", *m_member)
    CXX_BUS_ADD_TO_RULE_CHECK_OPTIONAL(rule, m_path, "path", m_path->GetPath())
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
  if (!res) return false;                            \
  if ((var).has_value())                             \
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
    CXX_BUS_CHECK_MATCH_OPTIONAL(
        matches, header.GetDestination(),
        m_destination.transform([](DBusUniqueConnectionName const& name) { return name.GetName(); }))

    std::unordered_map<uint8_t, std::string> stringArgs;
    std::unordered_map<uint8_t, ObjectPath> objectPathArgs;
    if (message.HasArguments())
    {
      uint32_t pointer{};
      uint8_t argIndex{};
      WalkSignature(*message.GetHeader().GetSignature(), message.GetRawData(), pointer, argIndex, stringArgs,
                    objectPathArgs);
    }

    for (ArgInfo const& argInfo : m_args)
    {
      // In case an earlier iteration set `matches` to false
      if (!matches)
      {
        return false;
      }

      if (auto it = stringArgs.find(argInfo.index); it != stringArgs.end())
      {
        matches &= it->second == argInfo.name;
      }
      else
      {
        return false;  // No need to check the rest. We don't match
      }
    }

    if (m_pathNamespace.has_value())
    {
      if (!header.GetObjectPath().has_value())
      {
        return false;
      }

      matches &= header.GetObjectPath()->GetPath().contains(m_pathNamespace->GetPath());
    }

    for (ArgInfo const& argInfo : m_argPaths)
    {
      // In case an earlier iteration set `matches` to false
      if (!matches)
      {
        return false;
      }

      std::string argObjectPath;
      if (auto it = objectPathArgs.find(argInfo.index); it != objectPathArgs.end())
      {
        argObjectPath = it->second.GetPath();
      }
      else if (auto it = stringArgs.find(argInfo.index); it != stringArgs.end())
      {
        argObjectPath = it->second;
      }
      else
      {
        return false;  // No need to check the rest. We don't match
      }

      std::vector<std::string> splitArgObjectPath = SplitString(argObjectPath, '/');
      std::vector<std::string> splitRuleArgObjectPath = SplitString(argInfo.name, '/');

      // clang-format off
      matches &=
      // Path must match completely
        (argInfo.name == argObjectPath ||
      // ===================== OR =====================
      // Rule must end with '/' and be a prefix of the message
        (argInfo.name.back() == '/' && IsPathPrefixed(splitRuleArgObjectPath, splitArgObjectPath) ) ||
      // ===================== OR =====================
      // Message must end with '/' and be a prefix of the rule
        (argObjectPath.back() == '/' && IsPathPrefixed(splitArgObjectPath, splitRuleArgObjectPath))
      );
      // clang-format on
    }

    if (m_argNamespace.has_value())
    {
      if (auto it = stringArgs.find(0); it != stringArgs.end())
      {
        matches &= it->second.starts_with(*m_argNamespace);
      }
      else
      {
        // First argument must exist and be a string, if it is not, then we don't match
        return false;
      }
    }

    return matches;
  }
}  // namespace cxxbus
