#include "DBusTypes.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "DBusHelpers.h"

namespace
{
  constexpr uint8_t MAX_DBUS_NAME_LENGTH = 255;

  // Returns std::nullopt if the provided name is valid
  // Returns a filled std::optional containing an error reason if the name is invalid
  std::optional<std::string> ValidateWellKnownName(std::string const& wellKnownName)
  {
    // A well-known name must be:
    //  - Non-empty
    //  - Not start with ':'
    //  - Not start with '.'
    //  - Composed of one or more elements separated by a '.'. All elements must be non-empty
    //  - Names must contain at least one '.' (and thus at least 2 elements)
    //  - Not be longer than 255

    if (wellKnownName.empty()) return "Well-known name cannot be empty";
    if (wellKnownName[0] == ':') return "Well-known name cannot start with ':'";
    if (wellKnownName[0] == '.') return "Well-known name cannot start with '.'";
    if (std::count(wellKnownName.begin(), wellKnownName.end(), '.') == 0) return "Well-known must contain at least 1 '.'";
    if (wellKnownName.size() >= MAX_DBUS_NAME_LENGTH) return "Well-known name must be shorter than 255 characters";

    return std::nullopt;
  }
}  // namespace

Signature::Signature(std::string signature)
  : m_signature(std::move(signature))
{
}

uint32_t Signature::Size() const
{
  return m_signature.size();
}

Signature::operator std::string() const
{
  return m_signature;
}

std::string const& Signature::GetSignature() const
{
  return m_signature;
}

// Get the alignment of the contained signature
uint8_t Signature::GetAlignmentOfSignature() const
{
  return ::GetAlignmentOfSignature(*this);
}

bool Signature::Empty() const
{
  return m_signature.empty();
}

bool Signature::operator==(std::string const& str) const
{
  return m_signature == str;
}

bool ObjectPath::Empty() const
{
  return m_path.empty();
}

DBusWellKnownName::DBusWellKnownName(std::string wellKnownName)
  : m_name()
{
  if (auto result{ValidateWellKnownName(wellKnownName)}; result.has_value())
  {
    throw InvalidDBusName{result.value()};
  }

  m_name = std::move(wellKnownName);
}

std::string const& DBusWellKnownName::GetName() const
{
  return m_name;
}
