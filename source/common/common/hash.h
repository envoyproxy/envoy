#pragma once

#include <string>
#include <type_traits>

#include "envoy/common/platform.h"

#include "source/common/common/macros.h"
#include "source/common/common/safe_memcpy.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
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
   * Return 64-bit hash from deterministically serializing a value in
   * an endian-independent way and hashing it with the xxHash algorithm.
   *
   * enums are excluded because they're not needed; if they were to be
   * supported later they should have a separate function because enum
   * sizes are implementation-specific.
   *
   * bools have an inlined specialization below because they too have
   * implementation-specific sizes.
   *
   * floating point values have a specialization because just endian-
   * ordering the binary leaves many possible binary representations of
   * NaN, for example.
   *
   * @param input supplies the value to hash.
   * @param seed supplies the hash seed which defaults to 0.
   * See https://github.com/Cyan4973/xxHash for details.
   */
  template <
      typename ValueType,
      std::enable_if_t<std::is_scalar_v<ValueType> && !std::is_enum_v<ValueType>, bool> = true>
  static uint64_t xxHash64Value(ValueType input, uint64_t seed = 0) {
#if defined(ABSL_IS_LITTLE_ENDIAN)
    return XXH64(reinterpret_cast<const char*>(&input), sizeof(input), seed);
#else
    char buf[sizeof(input)];
    const char* p = reinterpret_cast<const char*>(&input);
    for (size_t i = 0; i < sizeof(input); i++) {
      buf[i] = p[sizeof(input) - i - 1];
    }
    return XXH64(buf, sizeof(input), seed);
#endif
  }
  template <typename FloatingPoint,
            std::enable_if_t<std::is_floating_point_v<FloatingPoint>, bool> = true>
  static uint64_t xxHash64FloatingPoint(FloatingPoint input, uint64_t seed = 0) {
    if (std::isnan(input)) {
      return XXH64("NaN", 3, seed);
    }
    if (std::isinf(input)) {
      return XXH64("Inf", 3, seed);
    }
    int exp;
    FloatingPoint frac = std::frexp(input, &exp);
    seed = xxHash64Value(exp, seed);
    // Turn the fraction between -1 and 1 we have into an integer we can
    // hash endian-independently, using the largest possible range.
    int64_t mantissa = frac * 9223372036854775808.0; // 2^63
    return xxHash64Value(mantissa, seed);
  }

  /**
   * Return 64-bit hash from the xxHash algorithm for a collection of strings.
   * @param input supplies the absl::Span<absl::string_view> to hash.
   * @param seed supplies the hash seed which defaults to 0.
   * See https://github.com/Cyan4973/xxHash for details.
   */
  static uint64_t xxHash64(absl::Span<absl::string_view> input, uint64_t seed = 0);

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
      hash = ((hash << 5) + hash) + absl::ascii_tolower(c);
    };
    return hash;
  }
};

// Explicit specialization for bool because its size may be implementation dependent.
template <> inline uint64_t HashUtil::xxHash64Value(bool input, uint64_t seed) {
  char b = input ? 1 : 0;
  return XXH64(&b, sizeof(b), seed);
}
// Explicit specialization for float and double because IEEE binary representation of NaN
// can be inconsistent.
template <> inline uint64_t HashUtil::xxHash64Value(double input, uint64_t seed) {
  return xxHash64FloatingPoint(input, seed);
}
template <> inline uint64_t HashUtil::xxHash64Value(float input, uint64_t seed) {
  return xxHash64FloatingPoint(input, seed);
}

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
  static uint64_t murmurHash2(absl::string_view key, uint64_t seed = STD_HASH_SEED);

private:
  static inline uint64_t unalignedLoad(const char* p) {
    uint64_t result;
    safeMemcpyUnsafeSrc(&result, p);
    // byte swap to little endian on big endian platforms so hash values are the same regardless
    // of host endianness.
    return htole64(result);
  }

  // Loads n bytes in little endian format, where 1 <= n < 8.
  static inline uint64_t loadBytes(const char* p, int n) {
    uint64_t result = 0;
    --n;
    do {
      result = (result << 8) + static_cast<unsigned char>(p[n]);
    } while (--n >= 0);
    return result;
  }

  static inline uint64_t shiftMix(uint64_t v) { return v ^ (v >> 47); }
};

using SharedString = std::shared_ptr<std::string>;

struct HeterogeneousStringHash {
  // Specifying is_transparent indicates to the library infrastructure that
  // type-conversions should not be applied when calling find(), but instead
  // pass the actual types of the contained and searched-for objects directly to
  // these functors. See
  // https://en.cppreference.com/w/cpp/utility/functional/less_void for an
  // official reference, and https://abseil.io/tips/144 for a description of
  // using it in the context of absl.
  using is_transparent = void; // NOLINT(readability-identifier-naming)

  size_t operator()(absl::string_view a) const { return HashUtil::xxHash64(a); }
  size_t operator()(const SharedString& a) const { return HashUtil::xxHash64(*a); }
};

struct HeterogeneousStringEqual {
  // See description for HeterogeneousStringHash::is_transparent.
  using is_transparent = void; // NOLINT(readability-identifier-naming)

  size_t operator()(absl::string_view a, absl::string_view b) const { return a == b; }
  size_t operator()(const SharedString& a, const SharedString& b) const { return *a == *b; }
  size_t operator()(absl::string_view a, const SharedString& b) const { return a == *b; }
  size_t operator()(const SharedString& a, absl::string_view b) const { return *a == b; }
};

// We use heterogeneous hash/equal functors to do a find() without constructing
// a shared_string, which would entail making a full copy of the stat name.
using SharedStringSet =
    absl::flat_hash_set<SharedString, HeterogeneousStringHash, HeterogeneousStringEqual>;

// A special heterogeneous comparator is not needed for maps of strings; absl
// hashes allow for looking up a string-container with a string-view by default.
template <class Value> using StringMap = absl::flat_hash_map<std::string, Value>;

} // namespace Envoy
