#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
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
   * Return 64-bit hash representation of string ignoring case.
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
};

/**
 * From
 * (https://gcc.gnu.org/git/?p=gcc.git;a=blob_plain;f=libstdc%2b%2b-v3/libsupc%2b%2b/hash_bytes.cc).
 * Which is based on (https://sites.google.com/site/murmurhash/).
 */
class MurmurHash {
public:
  static const uint64_t STD_HASH_SEED = 0xc70f6907UL;
  /**
   * Return 64-bit hash from murmur hash2 as is implemented in std::hash<string>.
   * @param key supplies the string view
   * @param seed the seed to use for the hash
   * @return 64-bit hash representation of the supplied string view
   */
  static uint64_t murmurHash2_64(absl::string_view key, uint64_t seed = STD_HASH_SEED);

private:
  static inline uint64_t unaligned_load(const char* p) {
    uint64_t result;
    memcpy(&result, p, sizeof(result));
    return result;
  }

  // Loads n bytes, where 1 <= n < 8.
  static inline uint64_t load_bytes(const char* p, int n) {
    uint64_t result = 0;
    --n;
    do {
      result = (result << 8) + static_cast<unsigned char>(p[n]);
    } while (--n >= 0);
    return result;
  }

  static inline uint64_t shift_mix(uint64_t v) { return v ^ (v >> 47); }
};

struct ConstCharStarHash {
  size_t operator()(const char* a) const { return HashUtil::xxHash64(a); }
};

struct ConstCharStarEqual {
  size_t operator()(const char* a, const char* b) const { return strcmp(a, b) == 0; }
};

template <class Value>
using ConstCharStarHashMap =
    absl::flat_hash_map<const char*, Value, ConstCharStarHash, ConstCharStarEqual>;
using ConstCharStarHashSet =
    absl::flat_hash_set<const char*, ConstCharStarHash, ConstCharStarEqual>;

// Implements a set of nul-terminated char*, with the property that once
// inserted char* remain stable for the life of the set. Note there is
// currently no 'erase' method.
class StringSet {
public:
  ~StringSet();

  // Inserts a nul-terminated string, returning a stable char* reference to it.
  const char* insert(absl::string_view str);

  // Finds a stable reference for the specified string, returning nullptr if
  // it's not in the set yet.
  const char* find(const std::string& str) const { return find(str.c_str()); }
  const char* find(absl::string_view str) const { return find(std::string(str).c_str()); }
  const char* find(const char* str) const;

  // Returns the number of retained strings.
  size_t size() { return hash_set_.size(); }

private:
  absl::flat_hash_set<char*, ConstCharStarHash, ConstCharStarEqual> hash_set_;
};

} // namespace Envoy
