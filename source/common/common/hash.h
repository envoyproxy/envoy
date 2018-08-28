#pragma once

#include <string>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "xxhash.h"

namespace Envoy {

class HashUtil {
public:
  /**
   * Return 64-bit hash from the xxHash algorithm.
   * @param input supplies the string view to hash.
   * @param seed supplies the hash seed which defaults to 0.
   * See https://github.com/Cyan4973/xxHash for details.
   */
  static uint64_t xxHash64(absl::string_view input, uint64_t seed = 0) {
    return XXH64(input.data(), input.size(), seed);
  }

  /**
   * TODO(gsagula): extend xxHash to handle case-insensitive.
   *
   * Return 64-bit hash representation of string ingnoring case.
   * See djb2 (http://www.cse.yorku.ca/~oz/hash.html) for more details.
   * @param input supplies the string view.
   * @return 64-bit hash representation of the supplied string view.
   */
  static uint64_t djb2CaseInsensitiveHash(absl::string_view input) {
    uint64_t hash = 5381;
    for (unsigned char c : input) {
      hash += ((hash << 5) + hash) + absl::ascii_tolower(c);
    };
    return hash;
  }

  /**
   * Return 64-bit hash from a vector of uint32s.
   * @param input supplies the vector to be hashed.
   * Adapted from boost::hash_combine. See details here: https://stackoverflow.com/a/4948967
   * @return 64-bit hash of the supplied vector.
   */
  static uint64_t hashVector(std::vector<uint32_t> const& input) {
    std::size_t seed = input.size();
    for (auto& i : input) {
      seed ^= i + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
  }
};

} // namespace Envoy
