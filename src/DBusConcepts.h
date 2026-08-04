#pragma once

#include <concepts>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace cxxbus
{
  class Signature;
  class ObjectPath;
  class DBusInterfaceName;
  class Variant;
  template <typename... Ts>
  class MultipleCompleteTypes;

  namespace detail
  {
    template <typename T, template <typename...> typename Template>
    constexpr bool IS_SPECIALISATION = false;

    template <template <typename...> typename Template, typename... Args>
    constexpr bool IS_SPECIALISATION<Template<Args...>, Template> = true;
  }  // namespace detail

  template <typename T, template <typename...> typename Template>
  concept IsSpecialisation = detail::IS_SPECIALISATION<std::remove_cvref_t<T>, Template>;

  template <typename T>
  inline constexpr bool IsDBusBasicFixedType_v =
      std::disjunction_v<std::is_same<T, uint8_t>, std::is_same<T, bool>, std::is_same<T, int16_t>, std::is_same<T, uint16_t>, std::is_same<T, int32_t>,
                         std::is_same<T, uint32_t>, std::is_same<T, int64_t>, std::is_same<T, uint64_t>, std::is_same<T, float>, std::is_same<T, double>>;

  template <typename T>
  concept IsDBusBasicFixedType = IsDBusBasicFixedType_v<std::decay_t<T>>;

  template <typename T>
  inline constexpr bool IsStringType_v =
      std::disjunction_v<std::is_same<T, char const*>, std::is_same<T, char*>, std::is_same<T, char[]>,
                         std::is_same<T, std::string>, std::is_same<T, std::string_view>>;

  template <typename T>
  concept IsRawStringLiteral = std::same_as<std::decay_t<T>, char const*> || std::same_as<std::decay_t<T>, char*> || std::same_as<std::decay_t<T>, char[]>;

  template <typename T>
  concept IsString = IsStringType_v<std::decay_t<T>>;

  template <typename T>
  concept IsDBusBasicStringlikeType = IsString<std::decay_t<T>> || std::same_as<std::decay_t<T>, ObjectPath> || std::same_as<std::decay_t<T>, Signature> || std::same_as<std::decay_t<T>, DBusInterfaceName>;

  template <typename T>
  concept IsDBusMultipleCompleteTypes = IsSpecialisation<T, MultipleCompleteTypes>;

  template <typename T>
  concept IsDBusBasicType = IsDBusBasicFixedType<std::decay_t<T>> || IsDBusBasicStringlikeType<std::decay_t<T>> || IsDBusMultipleCompleteTypes<std::decay_t<T>>;

  template <typename T>
  concept IsDBusArray = IsSpecialisation<T, std::vector>;

  template <typename T>
  concept IsDBusStruct = IsSpecialisation<T, std::tuple>;

  template <typename T>
  concept IsDBusVariant = std::is_same_v<T, Variant>;

  template <typename T>
  concept IsDBusMap = IsSpecialisation<T, std::map>;

  template <typename T>
  concept IsDBusContainer = IsDBusArray<std::decay_t<T>> || IsDBusMap<std::decay_t<T>> || IsDBusStruct<std::decay_t<T>> || IsDBusVariant<std::decay_t<T>>;

  template <typename T>
  concept IsDBusType = IsDBusBasicType<std::decay_t<T>> || IsDBusContainer<std::decay_t<T>>;
}  // namespace cxxbus