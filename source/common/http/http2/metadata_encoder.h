#pragma once

#include <cstdint>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"
#include "common/http/http2/metadata_interface.h"

#include "nghttp2/nghttp2.h"

namespace Envoy {
namespace Http {
namespace Http2 {

/**
 * A class that creates and sends METADATA payload. The METADATA payload is a group of string key
 * value pairs encoded in HTTP/2 header blocks.
 */
class MetadataEncoder : Logger::Loggable<Logger::Id::http2> {
public:
  MetadataEncoder(uint64_t stream_id);

  /**
   * Creates wire format HTTP/2 header block from metadata_map. Only after previous payload is
   * drained, new payload can be encoded. This limitation is caused by nghttp2 can't incrementally
   * encode header block.
   * @param metadata_map supplies METADATA to encode.
   * @return whether encoding is successful.
   */
  bool createPayload(MetadataMap& metadata_map);

  /**
   * Called in nghttp2 callback function after METADATA is submitted. The function releases len data
   * in payload_.
   * @param len supplies the size to be released.
   */
  void releasePayload(uint64_t len);

  /**
   * @return true if there is payload to be submitted.
   */
  bool hasNextFrame();

  /**
   * Returns payload to be submitted.
   * @return the reference to payload.
   */
  Buffer::OwnedImpl& payload() { return payload_; }

  /**
   * TODO(soya3129): move those functions to MetadataHandler, since it's session level parameter.
   * Set max payload size for METADATA frame.
   * @param size supplies max frame size.
   */
  void setMaxMetadataSize(size_t size) { max_frame_size_ = size; }
  /**
   * Get max payload size for METADATA frame.
   * @return max frame size.
   */
  uint64_t getMaxMetadataSize() { return max_frame_size_; }

private:
  /**
   * Creates wired format header blocks using nghttp2.
   * @param metadata_map supplies METADATA to encode.
   * @return true if the creation succeeds.
   */
  bool createHeaderBlockUsingNghttp2(MetadataMap& metadata_map);

  // The METADATA payload to be sent.
  Buffer::OwnedImpl payload_;

  // Max payload size for METADATA frame.
  uint64_t max_frame_size_ = 16384;

  // Max payload size bound.
  const uint64_t max_payload_size_bound_ = 1024 * 1024;

  // The stream id the encoder is associated with.
  const uint64_t stream_id_;

  // Default HPACK table size.
  const size_t header_table_size_ = 4096;

  // TODO(soya3129): share deflater among all encoders in the same connection. The benefit is less
  // memory, and the caveat is encoding error on one stream can impact other streams.
  typedef CSmartPtr<nghttp2_hd_deflater, nghttp2_hd_deflate_del> Deflater;
  Deflater deflater_;
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
