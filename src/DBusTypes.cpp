#include "DBusTypes.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>

#include "DBusHelpers.h"

namespace cxxbus
{
  namespace
  {
    constexpr uint8_t MAX_DBUS_NAME_LENGTH = 255;

    // Returns std::nullopt if the provided name is valid
    // Returns a filled std::optional containing an error reason if the name is invalid
    std::optional<std::string> ValidateDBusName(std::string const& name, bool validateWellKnownName)
    {
      // A well-known name must be:
      //  - Non-empty
      //  - Not start with ':'
      //  - Not start with '.'
      //  - Composed of one or more elements separated by a '.'. All elements must be non-empty
      //  - Names must contain at least one '.' (and thus at least 2 elements)
      //  - Not be longer than 255

      std::string const prefix{validateWellKnownName ? "Well-known" : "Unique connection"};

      if (name.empty()) return std::format("{} name cannot be empty", prefix);
      if (validateWellKnownName && name[0] == ':') return "Well-known name cannot start with ':'";
      if (!validateWellKnownName && name[0] != ':') return "Unique connection name must start with ':'";
      if (name[0] == '.') return std::format("{} name cannot start with '.'", prefix);
      if (std::count(name.begin(), name.end(), '.') == 0) return std::format("{} must contain at least 1 '.'", prefix);
      if (name.size() >= MAX_DBUS_NAME_LENGTH) return std::format("{} name must be shorter than 255 characters", prefix);

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
    return ::cxxbus::GetAlignmentOfSignature(*this);
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

  DBusUniqueConnectionName::DBusUniqueConnectionName(std::string uniqueConnectionName)
    : m_name()
  {
    if (auto result{ValidateDBusName(uniqueConnectionName, false)}; result.has_value())
    {
      throw InvalidDBusName{result.value()};
    }

    m_name = std::move(uniqueConnectionName);
  }

  std::string const& DBusUniqueConnectionName::GetName() const
  {
    return m_name;
  }

  uint32_t DBusUniqueConnectionName::size() const
  {
    return m_name.size();
  }

  bool DBusUniqueConnectionName::empty() const
  {
    return m_name.empty();
  }

  DBusUniqueConnectionName::operator std::string() const
  {
    return m_name;
  }

  DBusWellKnownName::DBusWellKnownName(std::string wellKnownName)
    : m_name()
  {
    if (auto result{ValidateDBusName(wellKnownName, true)}; result.has_value())
    {
      throw InvalidDBusName{result.value()};
    }

    m_name = std::move(wellKnownName);
  }

  std::string const& DBusWellKnownName::GetName() const
  {
    return m_name;
  }

  uint32_t DBusWellKnownName::size() const
  {
    return m_name.size();
  }

  DBusWellKnownName::operator std::string() const
  {
    return m_name;
  }
}  // namespace cxxbus