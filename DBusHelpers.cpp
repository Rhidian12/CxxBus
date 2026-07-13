#include "DBusHelpers.h"

#include "DBusTypes.h"

bool IsDBusBasicFixedTypeCode(unsigned char c)
{
  switch (static_cast<DBusTypeCodes>(c))
  {
    case DBusTypeCodes::BYTE:
    case DBusTypeCodes::BOOLEAN:
    case DBusTypeCodes::INT16:
    case DBusTypeCodes::UINT16:
    case DBusTypeCodes::INT32:
    case DBusTypeCodes::UINT32:
    case DBusTypeCodes::INT64:
    case DBusTypeCodes::UINT64:
    case DBusTypeCodes::DOUBLE:
    case DBusTypeCodes::UNIX_FD:
      return true;
    default:
      return false;
  }
}

bool IsDBusBasicStringlikeTypeCode(unsigned char c)
{
  switch (static_cast<DBusTypeCodes>(c))
  {
    case DBusTypeCodes::STRING:
    case DBusTypeCodes::OBJECT_PATH:
    case DBusTypeCodes::SIGNATURE:
      return true;
    default:
      return false;
  }
}

bool IsDBusBasicTypeCode(unsigned char c)
{
  return IsDBusBasicFixedTypeCode(c) || IsDBusBasicStringlikeTypeCode(c);
}

bool IsDBusContainerTypeCode(unsigned char c)
{
  switch (static_cast<DBusTypeCodes>(c))
  {
    case DBusTypeCodes::ARRAY:
    case DBusTypeCodes::STRUCT_BEGIN:
    case DBusTypeCodes::STRUCT_END:
    case DBusTypeCodes::DICT_BEGIN:
    case DBusTypeCodes::DICT_END:
    case DBusTypeCodes::VARIANT:
      return true;
    default:
      return false;
  }
}

bool IsDBusTypeCode(unsigned char c)
{
  return IsDBusBasicTypeCode(c) || IsDBusContainerTypeCode(c);
}

bool IsDBusTypeCode(std::string const& str)
{
  for (unsigned char c : str)
  {
    if (!IsDBusTypeCode(c))
    {
      return false;
    }
  }

  return true;
}

bool AreDBusTypeCodeBracketsEven(std::string const& str)
{
  int32_t counter{};

  for (unsigned char c : str)
  {
    switch (static_cast<DBusTypeCodes>(c))
    {
      case DBusTypeCodes::DICT_BEGIN:
      case DBusTypeCodes::STRUCT_BEGIN:
        ++counter;
        break;
      case DBusTypeCodes::STRUCT_END:
      case DBusTypeCodes::DICT_END:
        --counter;
        break;
      default:
        break;
    }
  }

  return counter == 0;
}

uint8_t GetAlignmentOfSignature(Signature const& signature)
{
  switch (static_cast<DBusTypeCodes>(signature.GetSignature()[0]))
  {
    case DBusTypeCodes::BYTE:
    case DBusTypeCodes::SIGNATURE:
    case DBusTypeCodes::VARIANT:  // Alignment of Signature
      return 1;
    case DBusTypeCodes::INT16:
    case DBusTypeCodes::UINT16:
      return 2;
    case DBusTypeCodes::BOOLEAN:
    case DBusTypeCodes::UNIX_FD:
    case DBusTypeCodes::INT32:
    case DBusTypeCodes::UINT32:
    case DBusTypeCodes::STRING:
    case DBusTypeCodes::OBJECT_PATH:
    case DBusTypeCodes::ARRAY:
      return 4;
    case DBusTypeCodes::INT64:
    case DBusTypeCodes::UINT64:
    case DBusTypeCodes::DOUBLE:
    case DBusTypeCodes::STRUCT_BEGIN:
      return 8;
    case DBusTypeCodes::STRUCT:
    case DBusTypeCodes::STRUCT_END:
    case DBusTypeCodes::DICT:
    case DBusTypeCodes::DICT_BEGIN:
    case DBusTypeCodes::DICT_END:
    case DBusTypeCodes::INVALID:
      std::unreachable();
  }
}