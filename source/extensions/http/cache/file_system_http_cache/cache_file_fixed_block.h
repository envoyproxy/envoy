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
 * consistency. (It is not *expected* that a cache file will be handled by different
 * machines, but it costs little to accommodate it, and can simplify issue
 * investigation if the file format doesn't vary.)
 *
 * This data is serialized as a flat object rather than protobuf serialization because it
 * needs to be at the start of the file for efficient read, but write after the rest of the
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
   * fileId, cacheVersionId, headerSize and trailerSize serialize to 4 bytes each.
   * bodySize serializes to an 8-byte uint.
   * @return the size in bytes.
   */
  static constexpr uint32_t size() { return sizeof(uint32_t) * 4 + sizeof(uint64_t); }

  /**
   * fileId is a compile-time fixed value used to identify that this is a cache file.
   * @return the file ID.
   */
  std::array<char, 4> fileId() const { return file_id_; }

  /**
   * cacheVersionId is a compile-time fixed value that should be consistent between
   * versions of the file cache implementation. Changing version in code will
   * invalidate all cache entries where the version ID does not match.
   * @return the cache version ID.
   */
  std::array<char, 4> cacheVersionId() const { return cache_version_id_; }

  /**
   * the size of the serialized proto message capturing headers and metadata.
   * @return the size in bytes.
   */
  uint32_t headerSize() const { return header_size_; }

  /**
   * the size of the http body of the cache entry.
   * @return the size in bytes.
   */
  uint64_t bodySize() const { return body_size_; }

  /**
   * the size of the serialized proto message capturing trailers.
   * @return the size in bytes.
   */
  uint32_t trailerSize() const { return trailer_size_; }

  /**
   * sets the size of the serialized http headers, plus key and metadata, in the header block.
   * @param sz The size of the serialized headers, key and metadata.
   */
  void setHeadersSize(uint32_t sz) { header_size_ = sz; }

  /**
   * sets the size of the serialized body in the header block.
   * @param sz The size of the body data.
   */
  void setBodySize(uint64_t sz) { body_size_ = sz; }

  /**
   * sets the size of the serialized trailers in the header block.
   * @param sz The size of the serialized trailers.
   */
  void setTrailersSize(uint32_t sz) { trailer_size_ = sz; }

  /**
   * the offset from the start of the file to the start of the serialized headers proto.
   * @return the offset in bytes.
   */
  static uint32_t offsetToHeaders() { return size(); }

  /**
   * the offset from the start of the file to the start of the body data.
   * @return the offset in bytes.
   */
  uint32_t offsetToBody() const { return offsetToHeaders() + headerSize(); }

  /**
   * the offset from the start of the file to the start of the serialized trailers proto.
   * @return the offset in bytes.
   */
  uint64_t offsetToTrailers() const { return offsetToBody() + bodySize(); }

  /**
   * is this a valid cache file header block for the current code version?
   * @return True if the block's cache version id and file id match the current version.
   */
  bool isValid() const;

private:
  std::array<char, 4> file_id_;
  std::array<char, 4> cache_version_id_;
  uint32_t header_size_;
  uint32_t trailer_size_;
  uint64_t body_size_;
};

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
