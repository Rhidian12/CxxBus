#include "DBus.h"

namespace cxxbus
{
  template class MultipleCompleteTypes<std::string>;
  template class MultipleCompleteTypes<std::string, std::string>;
  template class MultipleCompleteTypes<std::string, std::string, Variant>;
  template class MultipleCompleteTypes<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t>;

  template std::vector<byte> MarshalDBusType<bool>(bool const&);
  template std::vector<byte> MarshalDBusType<uint8_t>(uint8_t const&);
  template std::vector<byte> MarshalDBusType<uint16_t>(uint16_t const&);
  template std::vector<byte> MarshalDBusType<int16_t>(int16_t const&);
  template std::vector<byte> MarshalDBusType<uint32_t>(uint32_t const&);
  template std::vector<byte> MarshalDBusType<int32_t>(int32_t const&);
  template std::vector<byte> MarshalDBusType<uint64_t>(uint64_t const&);
  template std::vector<byte> MarshalDBusType<int64_t>(int64_t const&);
  template std::vector<byte> MarshalDBusType<float>(float const&);
  template std::vector<byte> MarshalDBusType<double>(double const&);
  template std::vector<byte> MarshalDBusType<std::string>(std::string const&);
  template std::vector<byte> MarshalDBusType<std::string_view>(std::string_view const&);
  template std::vector<byte> MarshalDBusType<char const*>(char const* const&);
  template std::vector<byte> MarshalDBusType<Variant>(Variant const&);
  template std::vector<byte> MarshalDBusType<Signature>(Signature const&);
  template std::vector<byte> MarshalDBusType<ObjectPath>(ObjectPath const&);
  template std::vector<byte> MarshalDBusType<DBusInterfaceName>(DBusInterfaceName const&);

  template std::vector<byte> MarshalDBusType<std::vector<bool>>(std::vector<bool> const&);
  template std::vector<byte> MarshalDBusType<std::vector<uint8_t>>(std::vector<uint8_t> const&);
  template std::vector<byte> MarshalDBusType<std::vector<uint16_t>>(std::vector<uint16_t> const&);
  template std::vector<byte> MarshalDBusType<std::vector<int16_t>>(std::vector<int16_t> const&);
  template std::vector<byte> MarshalDBusType<std::vector<uint32_t>>(std::vector<uint32_t> const&);
  template std::vector<byte> MarshalDBusType<std::vector<int32_t>>(std::vector<int32_t> const&);
  template std::vector<byte> MarshalDBusType<std::vector<uint64_t>>(std::vector<uint64_t> const&);
  template std::vector<byte> MarshalDBusType<std::vector<int64_t>>(std::vector<int64_t> const&);
  template std::vector<byte> MarshalDBusType<std::vector<float>>(std::vector<float> const&);
  template std::vector<byte> MarshalDBusType<std::vector<double>>(std::vector<double> const&);
  template std::vector<byte> MarshalDBusType<std::vector<std::string>>(std::vector<std::string> const&);
  template std::vector<byte> MarshalDBusType<std::vector<std::string_view>>(std::vector<std::string_view> const&);
  template std::vector<byte> MarshalDBusType<std::vector<char const*>>(std::vector<char const*> const&);
  template std::vector<byte> MarshalDBusType<std::vector<Variant>>(std::vector<Variant> const&);
  template std::vector<byte> MarshalDBusType<std::vector<Signature>>(std::vector<Signature> const&);
  template std::vector<byte> MarshalDBusType<std::vector<ObjectPath>>(std::vector<ObjectPath> const&);
  template std::vector<byte> MarshalDBusType<std::vector<DBusInterfaceName>>(std::vector<DBusInterfaceName> const&);

  template std::vector<byte> MarshalDBusType<std::map<std::string, std::string>>(
      std::map<std::string, std::string> const&);
  template std::vector<byte> MarshalDBusType<std::map<std::string, Variant>>(std::map<std::string, Variant> const&);

  template std::vector<byte> MarshalDBusType<MultipleCompleteTypes<std::string>>(
      MultipleCompleteTypes<std::string> const&);
  template std::vector<byte> MarshalDBusType<MultipleCompleteTypes<std::string, std::string>>(
      MultipleCompleteTypes<std::string, std::string> const&);
  template std::vector<byte> MarshalDBusType<MultipleCompleteTypes<std::string, std::string, Variant>>(
      MultipleCompleteTypes<std::string, std::string, Variant> const&);
  template std::vector<byte>
  MarshalDBusType<MultipleCompleteTypes<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t>>(
      MultipleCompleteTypes<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t> const&);
}  // namespace cxxbus
