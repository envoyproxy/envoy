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
  ASSERT(s.size() == size() && size() == 24);
  // Bytewise copy the values from the string_view into the fields.
  // This ensures consistent endianness, and is the only safe way to copy values
  // from a byte array into value types.
  // chars must be cast to uint8_t first so that e.g. -1 becomes 0xff, before casting to uint64_t
  // - otherwise -1 becomes 0xffffffffffffffff.
  file_id_ = static_cast<uint32_t>(static_cast<uint8_t>(s[0])) << 24 |
             static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 16 |
             static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 8 |
             static_cast<uint32_t>(static_cast<uint8_t>(s[3]));
  cache_version_id_ = static_cast<uint32_t>(static_cast<uint8_t>(s[4])) << 24 |
                      static_cast<uint32_t>(static_cast<uint8_t>(s[5])) << 16 |
                      static_cast<uint32_t>(static_cast<uint8_t>(s[6])) << 8 |
                      static_cast<uint32_t>(static_cast<uint8_t>(s[7]));
  header_size_ = static_cast<uint32_t>(static_cast<uint8_t>(s[8])) << 24 |
                 static_cast<uint32_t>(static_cast<uint8_t>(s[9])) << 16 |
                 static_cast<uint32_t>(static_cast<uint8_t>(s[10])) << 8 |
                 static_cast<uint32_t>(static_cast<uint8_t>(s[11]));
  body_size_ = static_cast<uint64_t>(static_cast<uint8_t>(s[12])) << 56 |
               static_cast<uint64_t>(static_cast<uint8_t>(s[13])) << 48 |
               static_cast<uint64_t>(static_cast<uint8_t>(s[14])) << 40 |
               static_cast<uint64_t>(static_cast<uint8_t>(s[15])) << 32 |
               static_cast<uint64_t>(static_cast<uint8_t>(s[16])) << 24 |
               static_cast<uint64_t>(static_cast<uint8_t>(s[17])) << 16 |
               static_cast<uint64_t>(static_cast<uint8_t>(s[18])) << 8 |
               static_cast<uint64_t>(static_cast<uint8_t>(s[19]));
  trailer_size_ = static_cast<uint32_t>(static_cast<uint8_t>(s[20])) << 24 |
                  static_cast<uint32_t>(static_cast<uint8_t>(s[21])) << 16 |
                  static_cast<uint32_t>(static_cast<uint8_t>(s[22])) << 8 |
                  static_cast<uint32_t>(static_cast<uint8_t>(s[23]));
}

void CacheFileFixedBlock::serializeToBuffer(Buffer::Instance& buffer) {
  char b[size()];
  ASSERT(size() == 24);
  b[0] = file_id_ >> 24;
  b[1] = file_id_ >> 16 & 0xff;
  b[2] = file_id_ >> 8 & 0xff;
  b[3] = file_id_ & 0xff;
  b[4] = cache_version_id_ >> 24;
  b[5] = cache_version_id_ >> 16 & 0xff;
  b[6] = cache_version_id_ >> 8 & 0xff;
  b[7] = cache_version_id_ & 0xff;
  b[8] = header_size_ >> 24;
  b[9] = header_size_ >> 16 & 0xff;
  b[10] = header_size_ >> 8 & 0xff;
  b[11] = header_size_ & 0xff;
  b[12] = body_size_ >> 56;
  b[13] = body_size_ >> 48 & 0xff;
  b[14] = body_size_ >> 40 & 0xff;
  b[15] = body_size_ >> 32 & 0xff;
  b[16] = body_size_ >> 24 & 0xff;
  b[17] = body_size_ >> 16 & 0xff;
  b[18] = body_size_ >> 8 & 0xff;
  b[19] = body_size_ & 0xff;
  b[20] = trailer_size_ >> 24;
  b[21] = trailer_size_ >> 16 & 0xff;
  b[22] = trailer_size_ >> 8 & 0xff;
  b[23] = trailer_size_ & 0xff;
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
