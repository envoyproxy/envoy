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
constexpr std::array<char, 4> ExpectedFileId = {'C', 'A', 'C', 'H'};

// The expected next four bytes of the header - if cacheVersionId() doesn't match
// ExpectedCacheVersionId then the file is from an incompatible cache version and should
// be removed from the cache.
// Next 4 bytes of file should be "0000".
constexpr std::array<char, 4> ExpectedCacheVersionId = {'0', '0', '0', '0'};

} // namespace

CacheFileFixedBlock::CacheFileFixedBlock()
    : file_id_(ExpectedFileId), cache_version_id_(ExpectedCacheVersionId), header_size_(0),
      trailer_size_(0), body_size_(0) {}

void CacheFileFixedBlock::populateFromStringView(absl::string_view s) {
  // The string view should be the size of the buffer, and
  // Since we're explicitly reading byte offsets here, the size should match
  // what we read.
  // (This will remind us to change this function if we change the size
  // and vice-versa!)
  ASSERT(s.size() == size() && size() == 24);
  // Serialize the values from the string_view s into the member values.
  std::copy(s.begin(), s.begin() + 4, file_id_.begin());
  std::copy(s.begin() + 4, s.begin() + 8, cache_version_id_.begin());
  header_size_ = absl::big_endian::Load32(&s[8]);
  trailer_size_ = absl::big_endian::Load32(&s[12]);
  body_size_ = absl::big_endian::Load64(&s[16]);
}

void CacheFileFixedBlock::serializeToBuffer(Buffer::Instance& buffer) {
  char b[size()];
  // Since we're explicitly writing byte offsets here, the size should match
  // what we write.
  // (This will remind us to change this function if we change the size
  // and vice-versa!)
  ASSERT(size() == 24);
  // Serialize the values from the member values into the stack buffer b.
  std::copy(file_id_.begin(), file_id_.end(), &b[0]);
  std::copy(cache_version_id_.begin(), cache_version_id_.end(), &b[4]);
  absl::big_endian::Store32(&b[8], header_size_);
  absl::big_endian::Store32(&b[12], trailer_size_);
  absl::big_endian::Store64(&b[16], body_size_);
  // Append that buffer into the target buffer object.
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
