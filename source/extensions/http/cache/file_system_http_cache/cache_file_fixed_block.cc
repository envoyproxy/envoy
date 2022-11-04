#include "source/extensions/http/cache/file_system_http_cache/cache_file_fixed_block.h"

#include "source/common/common/assert.h"
#include "source/common/common/safe_memcpy.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

namespace {
uint64_t ntohl64(uint64_t n) {
#if defined(ABSL_IS_LITTLE_ENDIAN)
  // in-place byte order swap for a uint64.
  // Optimization in clang or gcc can recognize this as a single `bswap` instruction.
  n = ((n & 0xFF00000000000000u) >> 56u) | ((n & 0x00FF000000000000u) >> 40u) |
      ((n & 0x0000FF0000000000u) >> 24u) | ((n & 0x000000FF00000000u) >> 8u) |
      ((n & 0x00000000FF000000u) << 8u) | ((n & 0x0000000000FF0000u) << 24u) |
      ((n & 0x000000000000FF00u) << 40u) | ((n & 0x00000000000000FFu) << 56u);
#endif
  return n;
}
uint64_t htonl64(uint64_t n) { return ntohl64(n); }

uint32_t ntohl32(uint32_t n) {
#if defined(ABSL_IS_LITTLE_ENDIAN)
  // in-place byte order swap for a uint32.
  // Optimization in clang or gcc can recognize this as a single `bswap` instruction.
  n = ((n & 0xFF000000u) >> 24u) | ((n & 0x00FF0000u) >> 8u) | ((n & 0x0000FF00u) << 8u) |
      ((n & 0x000000FFu) << 24u);
#endif
  return n;
}
uint64_t htonl32(uint32_t n) { return ntohl32(n); }
} // namespace

// Beginning of file should be "CACH".
const uint32_t CacheFileFixedBlock::expectedFileId =
    (static_cast<uint32_t>('C') << 24) + (static_cast<uint32_t>('A') << 16) +
    (static_cast<uint32_t>('C') << 8) + static_cast<uint32_t>('H');

// Next 4 bytes of file should be "0000".
// Increment this to invalidate old cache files if the format changes.
// Formatted string-style rather than as an actual int to make it easily human-readable.
const uint32_t CacheFileFixedBlock::expectedCacheVersionId =
    (static_cast<uint32_t>('0') << 24) + (static_cast<uint32_t>('0') << 16) +
    (static_cast<uint32_t>('0') << 8) + static_cast<uint32_t>('0');

CacheFileFixedBlock::CacheFileFixedBlock() {
  setFileId(expectedFileId);
  setCacheVersionId(expectedCacheVersionId);
  setHeadersSize(0);
  setTrailersSize(0);
  setBodySize(0);
}

void CacheFileFixedBlock::populateFromStringView(absl::string_view s) {
  ASSERT(s.size() == size());
  safeMemcpyUnsafeSrc(&contents_, s.begin());
}

uint32_t CacheFileFixedBlock::getUint32(const uint32_t& t) const { return ntohl32(t); }
uint64_t CacheFileFixedBlock::getUint64(const uint64_t& t) const { return ntohl64(t); }
void CacheFileFixedBlock::setUint32(uint32_t& t, uint32_t v) { t = htonl32(v); }
void CacheFileFixedBlock::setUint64(uint64_t& t, uint64_t v) { t = htonl64(v); }

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
