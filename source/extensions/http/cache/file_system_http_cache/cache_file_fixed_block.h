#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>

#include "source/common/common/byte_order.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

// This represents a minimal header block on the cache entry file; it uses a struct that
// is required to be packed, and explicit network byte order, to ensure consistency.
class CacheFileFixedBlock {
public:
  CacheFileFixedBlock();

  // Deserializes the string representation of a CacheFileFixedBlock into this instance.
  void populateFromStringView(absl::string_view s);

  // Returns the size in bytes of a CacheFileFixedBlock.
  static size_t size() { return sizeof(contents_); }

  // fileId is a fixed value used to identify that this is a cache file.
  uint32_t fileId() const { return fromEndianness<ByteOrder::BigEndian>(contents_.file_id_); }

  // cacheVersionId is a value that should be consistent between versions of the file
  // cache implementation. Changing version in code will invalidate all cache
  // entries where the version ID does not match.
  uint32_t cacheVersionId() const {
    return fromEndianness<ByteOrder::BigEndian>(contents_.cache_version_id_);
  }

  // Size of the serialized proto message capturing headers and metadata.
  size_t headerSize() const { return fromEndianness<ByteOrder::BigEndian>(contents_.header_size_); }

  // Size of the serialized proto message capturing trailers.
  size_t trailerSize() const {
    return fromEndianness<ByteOrder::BigEndian>(contents_.trailer_size_);
  }

  // Size of the http body of the cache entry.
  size_t bodySize() const { return fromEndianness<ByteOrder::BigEndian>(contents_.body_size_); }

  // Sets the size of the serialized headers, key and metadata.
  void setHeadersSize(size_t sz) {
    contents_.header_size_ = toEndianness<ByteOrder::BigEndian>(static_cast<uint32_t>(sz));
  }

  // Sets the size of the serialized trailers.
  void setTrailersSize(size_t sz) {
    contents_.trailer_size_ = toEndianness<ByteOrder::BigEndian>(static_cast<uint32_t>(sz));
  }

  // Sets the size of the serialized body.
  void setBodySize(size_t sz) {
    contents_.body_size_ = toEndianness<ByteOrder::BigEndian>(static_cast<uint64_t>(sz));
  }

  // Returns the offset from the start of the file to the start of the
  // serialized headers proto.
  static size_t offsetToHeaders() { return size(); }

  // Returns the offset from the start of the file to the start of the body data.
  size_t offsetToBody() const { return offsetToHeaders() + headerSize(); }

  // Returns the offset from the start of the file to the start of the
  // serialized trailers proto.
  size_t offsetToTrailers() const { return offsetToBody() + bodySize(); }

  // Returns a string_view of the serialized fixed header chunk.
  absl::string_view stringView() const { return {contents_.as_str_, size()}; }

  // Returns true if the fileId and cacheVersionId match the compile-time constants from
  // cache_file_fixed_block.cc
  bool isValid() const;

private:
  // sets the fileId, a fixed value used to identify that this is a cache file.
  // This is private so only the unit test can call it, as the file ID should be
  // a compile-time constant in ordinary use.
  void setFileId(uint32_t id) { contents_.file_id_ = toEndianness<ByteOrder::BigEndian>(id); }

  // sets the cacheVersionId, a value that should be consistent between versions
  // of the file cache implementation.
  // This is private so only the unit test can call it, as the version ID should be
  // a compile-time constant in ordinary use.
  void setCacheVersionId(uint32_t id) {
    contents_.cache_version_id_ = toEndianness<ByteOrder::BigEndian>(id);
  }

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

  friend class CacheFileFixedBlockTest;
};

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
