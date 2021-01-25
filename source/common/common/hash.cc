#include "common/common/hash.h"

#include "common/common/assert.h"

#include "absl/strings/string_view.h"

namespace Envoy {

HashUtil::XxHash64::XxHash64(uint64_t seed) {
  xx_state_.reset(XXH64_createState());
  XXH64_reset(xx_state_.get(), seed);
}

HashUtil::XxHash64& HashUtil::XxHash64::update(absl::string_view data) {
  XXH64_update(xx_state_.get(), data.data(), data.length());
  return *this;
}

uint64_t HashUtil::XxHash64::digest() const { return XXH64_digest(xx_state_.get()); }

void HashUtil::XxHash64::XxDeleter::operator()(XXH64_state_t* state) { XXH64_freeState(state); }

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
