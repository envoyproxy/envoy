#include "source/extensions/http/cache/file_system_http_cache/cache_file_fixed_block.h"

#include "source/common/common/assert.h"
#include "source/common/common/safe_memcpy.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

namespace {
// The expected first four bytes of the header - if fileId() doesn't match ExpectedFileId
// then the file is not a cache file and should be removed from the cache.
// Beginning of file should be "CACH".
constexpr uint32_t ExpectedFileId = (static_cast<uint32_t>('C') << 24) |
                                    (static_cast<uint32_t>('A') << 16) |
                                    (static_cast<uint32_t>('C') << 8) | static_cast<uint32_t>('H');

// The expected next four bytes of the header - if cacheVersionId() doesn't match
// ExpectedCacheVersionId then the file is from an incompatible cache version and should
// be removed from the cache.
// Next 4 bytes of file should be "0000".
// Increment this to invalidate old cache files if the format changes.
// Formatted string-style rather than as an actual int to make it easily human-readable.
constexpr uint32_t ExpectedCacheVersionId =
    (static_cast<uint32_t>('0') << 24) | (static_cast<uint32_t>('0') << 16) |
    (static_cast<uint32_t>('0') << 8) | static_cast<uint32_t>('0');

} // namespace

CacheFileFixedBlock::CacheFileFixedBlock() {
  setFileId(ExpectedFileId);
  setCacheVersionId(ExpectedCacheVersionId);
  setHeadersSize(0);
  setTrailersSize(0);
  setBodySize(0);
}

void CacheFileFixedBlock::populateFromStringView(absl::string_view s) {
  ASSERT(s.size() == size());
  safeMemcpyUnsafeSrc(&contents_, s.begin());
}

bool CacheFileFixedBlock::isValid() const {
  return fileId() == ExpectedFileId && cacheVersionId() == ExpectedCacheVersionId;
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
