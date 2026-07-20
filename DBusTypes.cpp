#include "DBusTypes.h"

#include "DBusHelpers.h"

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

bool ObjectPath::Empty() const
{
  return m_path.empty();
}