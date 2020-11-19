#include "twem_hash.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

static uint64_t FNV_64_INIT = UINT64_C(0xcbf29ce484222325);
static uint64_t FNV_64_PRIME = UINT64_C(0x100000001b3);

uint32_t TwemHash::fnv1a64(std::string key) {
  uint32_t hash = static_cast<uint32_t>(FNV_64_INIT);
  size_t x;

  for (x = 0; x < key.length(); x++) {
    uint32_t val = static_cast<uint32_t>(key[x]);
    hash ^= val;
    hash *= static_cast<uint32_t>(FNV_64_PRIME);
  }

  return hash;
}

uint32_t TwemHash::hash(absl::string_view key, uint32_t alignment) {
  unsigned char results[16];

  const char* buffer = static_cast<const char*>(key.data());

  MD5_CTX md5;
  MD5_Init(&md5);
  MD5_Update(&md5, buffer, key.size());
  MD5_Final(results, &md5);

  return (static_cast<uint32_t>(results[3 + alignment * 4] & 0xFF) << 24)
           | (static_cast<uint32_t>(results[2 + alignment * 4] & 0xFF) << 16)
           | (static_cast<uint32_t>(results[1 + alignment * 4] & 0xFF) << 8)
           | (results[0 + alignment * 4] & 0xFF);
}

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
