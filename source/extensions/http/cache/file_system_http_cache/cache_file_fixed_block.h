#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>

#include "envoy/buffer/buffer.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

/**
 * CacheFileFixedBlock represents a minimal header block on the cache entry file; it
 * uses a struct that is required to be packed, and explicit byte order, to ensure
 * consistency.
 *
 * This data is serialized as a flat object rather than protobuf serialization because it
 * needs to be readable from the start of the file, but writeable after the rest of the
 * file has been completely written (as the body size and trailer size aren't necessarily
 * known until the entire content has been streamed). Serialized proto messages can
 * change size when values change, which makes them unsuited for this purpose.
 */
class CacheFileFixedBlock {
public:
  /**
   * the constructor initializes with the current compile-time constant values
   * for fileId and cacheVersionId, and zero for all other member values.
   */
  CacheFileFixedBlock();

  /**
   * deserializes the string representation of a CacheFileFixedBlock into this instance.
   * @param str The string_view from which to populate the block.
   */
  void populateFromStringView(absl::string_view str);

  /**
   * appends the serialized fixed header chunk onto a buffer.
   * @param buffer the buffer onto which to append the serialized fixed header chunk.
   */
  void serializeToBuffer(Buffer::Instance& buffer);

  /**
   * the size in bytes of a serialized CacheFileFixedBlock. This is compile-time constant.
   * fileId, cacheVersionId, headerSize and trailerSize serialize to 4-byte uints.
   * bodySize serializes to an 8-byte uint.
   * @return the size in bytes.
   */
  static constexpr size_t size() { return sizeof(uint32_t) * 4 + sizeof(uint64_t); }

  /**
   * fileId is a compile-time fixed value used to identify that this is a cache file.
   * @return the file ID.
   */
  uint32_t fileId() const { return file_id_; }

  /**
   * cacheVersionId is a compile-time fixed value that should be consistent between
   * versions of the file cache implementation. Changing version in code will
   * invalidate all cache entries where the version ID does not match.
   * @return the cache version ID.
   */
  uint32_t cacheVersionId() const { return cache_version_id_; }

  /**
   * the size of the serialized proto message capturing headers and metadata.
   * @return the size in bytes.
   */
  size_t headerSize() const { return header_size_; }

  /**
   * the size of the serialized proto message capturing trailers.
   * @return the size in bytes.
   */
  size_t trailerSize() const { return trailer_size_; }

  /**
   * the size of the http body of the cache entry.
   * @return the size in bytes.
   */
  size_t bodySize() const { return body_size_; }

  /**
   * sets the size of the serialized http headers, plus key and metadata, in the header block.
   * @param sz The size of the serialized headers, key and metadata.
   */
  void setHeadersSize(size_t sz) { header_size_ = sz; }

  /**
   * sets the size of the serialized trailers in the header block.
   * @param sz The size of the serialized trailers.
   */
  void setTrailersSize(size_t sz) { trailer_size_ = sz; }

  /**
   * sets the size of the serialized body in the header block.
   * @param sz The size of the body data.
   */
  void setBodySize(size_t sz) { body_size_ = sz; }

  /**
   * the offset from the start of the file to the start of the serialized headers proto.
   * @return the offset in bytes.
   */
  static off_t offsetToHeaders() { return size(); }

  /**
   * the offset from the start of the file to the start of the body data.
   * @return the offset in bytes.
   */
  off_t offsetToBody() const { return offsetToHeaders() + headerSize(); }

  /**
   * the offset from the start of the file to the start of the serialized trailers proto.
   * @return the offset in bytes.
   */
  off_t offsetToTrailers() const { return offsetToBody() + bodySize(); }

  /**
   * is this a valid cache file header block for the current code version?
   * @return True if the block's cache version id and file id match the current version.
   */
  bool isValid() const;

private:
  /**
   * sets the fileId, a fixed value used to identify that this is a cache file.
   * This is private so only the unit test can call it, as the file ID should be
   * a compile-time constant in ordinary use.
   * @param id The file ID to set.
   */
  void setFileId(uint32_t id) { file_id_ = id; }

  /**
   * sets the cacheVersionId, a value that should be consistent between versions
   * of the file cache implementation.
   * This is private so only the unit test can call it, as the version ID should be
   * a compile-time constant in ordinary use.
   * @param id The cache version ID to set.
   */
  void setCacheVersionId(uint32_t id) { cache_version_id_ = id; }

  uint32_t file_id_;
  uint32_t cache_version_id_;
  size_t header_size_;
  size_t trailer_size_;
  size_t body_size_;

  friend class CacheFileFixedBlockTest;
};

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
