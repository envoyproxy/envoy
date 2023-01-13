#pragma once

#include <cassert>
#include <charconv>
#include <limits>
#include <string>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

template <typename Integer> std::string hex(Integer value) {
  // 4 bits per hex digit char, and then +1 char for possible minus sign
  char buffer[std::numeric_limits<Integer>::digits / 4 + 1];

  const int base = 16;
  auto result = std::to_chars(std::begin(buffer), std::end(buffer), value, base);
  assert(result.ec == std::errc());

  return std::string{std::begin(buffer), result.ptr};
}

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
