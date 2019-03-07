#include "common/common/hash.h"

#include "absl/strings/string_view.h"

namespace Envoy {

// Computes a 64-bit murmur hash 2, only works with 64-bit platforms. Revisit if support for 32-bit
// platforms are needed.
// from
// (https://gcc.gnu.org/git/?p=gcc.git;a=blob_plain;f=libstdc%2b%2b-v3/libsupc%2b%2b/hash_bytes.cc)
uint64_t MurmurHash::murmurHash2_64(absl::string_view key, uint64_t seed) {
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

StringSet::~StringSet() {
  std::vector<char*> keys;
  keys.reserve(hash_set_.size());
  for (char* p : hash_set_) {
    keys.push_back(p);
  }
  hash_set_.clear();
  for (char* p : keys) {
    delete[] p;
  }
}

const char* StringSet::insert(absl::string_view str) {
  char* p = new char[str.size() + 1];
  memcpy(p, str.data(), str.size());
  p[str.size()] = '\0';
  auto insertion = hash_set_.insert(p);
  if (!insertion.second) {
    delete[] p;
    return *insertion.first;
  }
  return p;
}

const char* StringSet::find(const char* str) const {
  auto iter = hash_set_.find(const_cast<char*>(str));
  if (iter == hash_set_.end()) {
    return nullptr;
  }
  return *iter;
}

} // namespace Envoy
