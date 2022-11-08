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

// Deserialize 4 bytes into a uint32_t.
uint32_t deserializeUint32(const char* p) {
  return static_cast<uint32_t>(static_cast<uint8_t>(p[0])) << 24 |
         static_cast<uint32_t>(static_cast<uint8_t>(p[1])) << 16 |
         static_cast<uint32_t>(static_cast<uint8_t>(p[2])) << 8 |
         static_cast<uint32_t>(static_cast<uint8_t>(p[3]));
}

// Deserialize 8 bytes into a uint64_t.
uint64_t deserializeUint64(const char* p) {
  return static_cast<uint64_t>(deserializeUint32(p)) | deserializeUint32(p + 4);
}

// Serialize a uint32 into 4 bytes at pointer p.
void serializeUint32(uint32_t value, char* p) {
  p[0] = value >> 24;
  p[1] = value >> 16 & 0xff;
  p[2] = value >> 8 & 0xff;
  p[3] = value & 0xff;
}

// Serialize a uint64 into 8 bytes at pointer p.
void serializeUint64(uint64_t value, char* p) {
  serializeUint32(value >> 32, p);
  serializeUint32(value & 0xffffffff, p + 4);
}

} // namespace

CacheFileFixedBlock::CacheFileFixedBlock() {
  setFileId(ExpectedFileId);
  setCacheVersionId(ExpectedCacheVersionId);
  setHeadersSize(0);
  setTrailersSize(0);
  setBodySize(0);
}

void CacheFileFixedBlock::populateFromStringView(absl::string_view s) {
  ASSERT(s.size() == size() && size() == 24);
  // Bytewise copy the values from the string_view into the fields.
  // This ensures consistent endianness, and is the only safe way to copy values
  // from a byte array into value types.
  // chars must be cast to uint8_t first so that e.g. -1 becomes 0xff, before casting to uint64_t
  // - otherwise -1 becomes 0xffffffffffffffff.
  file_id_ = deserializeUint32(&s[0]);
  cache_version_id_ = deserializeUint32(&s[4]);
  header_size_ = deserializeUint32(&s[8]);
  body_size_ = deserializeUint64(&s[12]);
  trailer_size_ = deserializeUint32(&s[20]);
}

void CacheFileFixedBlock::serializeToBuffer(Buffer::Instance& buffer) {
  char b[size()];
  ASSERT(size() == 24);
  serializeUint32(file_id_, &b[0]);
  serializeUint32(cache_version_id_, &b[4]);
  serializeUint32(header_size_, &b[8]);
  serializeUint64(body_size_, &b[12]);
  serializeUint32(trailer_size_, &b[20]);
  buffer.add(absl::string_view{b, size()});
}

bool CacheFileFixedBlock::isValid() const {
  return fileId() == ExpectedFileId && cacheVersionId() == ExpectedCacheVersionId;
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
