#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

// This represents a minimal header block on the cache entry file; it uses a struct that
// is required to be packed, and explicit network byte order, to ensure compatibility.
class CacheFileFixedBlock {
public:
  CacheFileFixedBlock();
  void populateFromStringView(absl::string_view s);
  static size_t size() { return sizeof(contents_); }
  // The expected first four bytes of the header - if fileId() doesn't match expectedFileId()
  // then the file is not a cache file and should be removed from the cache.
  static const uint32_t expectedFileId;
  // The expected next four bytes of the header - if cacheVersionId() doesn't match
  // expectedCacheVersionId() then the file is from an incompatible cache version and should
  // be removed from the cache.
  static const uint32_t expectedCacheVersionId;
  // fileId is a fixed value used to identify that this is a cache file.
  uint32_t fileId() const { return getUint32(contents_.file_id_); }
  // cacheVersionId is a value that should be consistent between versions of the file
  // cache implementation. Changing version in code will invalidate at startup all cache
  // entries where the version ID does not match.
  uint32_t cacheVersionId() const { return getUint32(contents_.cache_version_id_); }
  // Size of the serialized proto message capturing headers and metadata.
  size_t headerSize() const { return getUint32(contents_.header_size_); }
  // Size of the serialized proto message capturing trailers.
  size_t trailerSize() const { return getUint32(contents_.trailer_size_); }
  // Size of the http body of the cache entry.
  size_t bodySize() const { return getUint64(contents_.body_size_); }
  void setFileId(uint32_t id) { setUint32(contents_.file_id_, id); }
  void setCacheVersionId(uint32_t id) { setUint32(contents_.cache_version_id_, id); }
  void setHeadersSize(size_t sz) { setUint32(contents_.header_size_, sz); }
  void setTrailersSize(size_t sz) { setUint32(contents_.trailer_size_, sz); }
  void setBodySize(size_t sz) { setUint64(contents_.body_size_, sz); }
  static size_t offsetToBody() { return size(); }
  size_t offsetToTrailers() const { return offsetToBody() + bodySize(); }
  size_t offsetToHeaders() const { return offsetToTrailers() + trailerSize(); }
  absl::string_view stringView() const { return {contents_.as_str_, size()}; }

private:
  union {
    struct {
      uint32_t file_id_;
      uint32_t cache_version_id_;
      uint32_t header_size_;
      uint32_t trailer_size_;
      uint64_t body_size_;
    };
    char as_str_[1];
  } contents_;
  static_assert(sizeof(contents_) ==
                    sizeof(contents_.file_id_) + sizeof(contents_.cache_version_id_) +
                        sizeof(contents_.header_size_) + sizeof(contents_.trailer_size_) +
                        sizeof(contents_.body_size_),
                "contents_ must be fully packed for consistency");
  uint32_t getUint32(const uint32_t& t) const;
  uint64_t getUint64(const uint64_t& t) const;
  void setUint32(uint32_t& t, uint32_t v);
  void setUint64(uint64_t& t, uint64_t v);
};

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
