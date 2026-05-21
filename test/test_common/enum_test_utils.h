#pragma once

#include <type_traits>

#include "absl/base/casts.h"

namespace Envoy {

template <class Enum> Enum uncheckedEnumCastForTest(std::underlying_type_t<Enum> value) {
  static_assert(std::is_enum_v<Enum>);
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  return static_cast<Enum>(value);
}

} // namespace Envoy
