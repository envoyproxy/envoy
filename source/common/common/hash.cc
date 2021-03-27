#include "common/common/hash.h"

#include "envoy/common/exception.h"

#include "absl/strings/string_view.h"

namespace Envoy {

uint64_t HashUtil::xxHash64(const std::vector<std::string>& input, uint64_t seed) {
  XXH64_state_t* const state = XXH64_createState();
  if (state == NULL) {
    throw EnvoyException("create XXH64 state failed");
  };
  if (XXH64_reset(state, seed) == XXH_ERROR) {
    XXH64_freeState(state);
    throw EnvoyException("XH64 reset seed failed");
  }
  for (size_t i = 0; i < input.size(); i++) {
    if (XXH64_update(state, input[i].c_str(), input[i].size()) == XXH_ERROR) {
      XXH64_freeState(state);
      throw EnvoyException("XH64 hash update failed");
    };
  }
  auto hash = XXH64_digest(state);
  XXH64_freeState(state);
  return hash;
}

// Computes a 64-bit murmur hash 2, only works with 64-bit platforms. Revisit if support for 32-bit
// platforms are needed.
// from
// (https://gcc.gnu.org/git/?p=gcc.git;a=blob_plain;f=libstdc%2b%2b-v3/libsupc%2b%2b/hash_bytes.cc)
uint64_t MurmurHash::murmurHash2(absl::string_view key, uint64_t seed) {
  static const uint64_t mul = 0xc6a4a7935bd1e995UL;
  const char* const buf = static_cast<const char*>(key.data());
  uint64_t len = key.size();

  // Remove the bytes not divisible by the sizeof(uint64_t). This
  // allows the main loop to process the data as 64-bit integers.
  const int len_aligned = len & ~0x7;
  const char* const end = buf + len_aligned;
  uint64_t hash = seed ^ (len * mul);
  for (const char* p = buf; p != end; p += 8) {
    const uint64_t data = shiftMix(unalignedLoad(p) * mul) * mul;
    hash ^= data;
    hash *= mul;
  }

  if ((len & 0x7) != 0) {
    const uint64_t data = loadBytes(end, len & 0x7);
    hash ^= data;
    hash *= mul;
  }
  hash = shiftMix(hash) * mul;
  hash = shiftMix(hash);
  return hash;
}

} // namespace Envoy
