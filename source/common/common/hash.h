#pragma once

#include <string>

#include "xxhash.h"

namespace Envoy {

class HashUtil {
public:
  /**
   * Return 64-bit hash with seed of 0 from the xxHash algorithm.
   * See https://github.com/Cyan4973/xxHash for details.
   */
  static uint64_t xxHash64(const std::string& input) {
    return XXH64(input.c_str(), input.size(), 0);
  }
};

} // namespace Envoy
