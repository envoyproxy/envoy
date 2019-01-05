#include "common/common/hash.h"

#include "absl/strings/string_view.h"

namespace {

static inline std::uint64_t unaligned_load(const char* p) {
  std::uint64_t result;
  __builtin_memcpy(&result, p, sizeof(result));
  return result;
}

// Loads n bytes, where 1 <= n < 8.
static inline std::uint64_t load_bytes(const char* p, int n) {
  std::uint64_t result = 0;
  --n;
  do {
    result = (result << 8) + static_cast<unsigned char>(p[n]);
  } while (--n >= 0);
  return result;
}

static inline std::uint64_t shift_mix(std::uint64_t v) { return v ^ (v >> 47); }

} // namespace

namespace Envoy {

uint64_t HashUtil::murmurHash2_64(absl::string_view key, uint64_t seed) {
  static const uint64_t mul = 0xc6a4a7935bd1e995UL;
  const char* const buf = static_cast<const char*>(key.data());
  uint64_t len = key.size();

  // Remove the bytes not divisible by the sizeof(uint64_t). This
  // allows the main loop to process the data as 64-bit integers.
  const int len_aligned = len & ~0x7;
  const char* const end = buf + len_aligned;
  uint64_t hash = seed ^ (len * mul);
  for (const char* p = buf; p != end; p += 8) {
    const uint64_t data = shift_mix(unaligned_load(p) * mul) * mul;
    hash ^= data;
    hash *= mul;
  }

  if ((len & 0x7) != 0) {
    const uint64_t data = load_bytes(end, len & 0x7);
    hash ^= data;
    hash *= mul;
  }
  hash = shift_mix(hash) * mul;
  hash = shift_mix(hash);
  return hash;
}

} // namespace Envoy
