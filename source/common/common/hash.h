#pragma once

#include <string>

#include "absl/strings/string_view.h"
#include "xxhash.h"

namespace Envoy {

class HashUtil {
public:
  /**
   * Return 64-bit hash with seed of 0 from the xxHash algorithm.
   * See https://github.com/Cyan4973/xxHash for details.
   */
  static uint64_t xxHash64(absl::string_view input) { return XXH64(input.data(), input.size(), 0); }
};

} // namespace Envoy
