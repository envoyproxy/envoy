#pragma once

#include <cstdint>
#include <queue>
#include <string>

#include "envoy/http/codec.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/c_smart_ptr.h"
#include "common/common/logger.h"

#include "nghttp2/nghttp2.h"

namespace Envoy {
namespace Http {
namespace Http2 {

/**
 * A class that creates and sends METADATA payload. The METADATA payload is a group of string key
 * value pairs encoded in HTTP/2 header blocks. METADATA frames are constructed in two steps: first,
 * the stream submits the frames' headers to nghttp2, and later, when nghttp2 prepares to send the
 * frames, it calls back into this class in order to construct their payloads.
 */
class MetadataEncoder : Logger::Loggable<Logger::Id::http2> {
public:
  MetadataEncoder();

  /**
   * Creates wire format HTTP/2 header block from a vector of metadata maps.
   * @param metadata_map_vector supplies the metadata map vector to encode.
   * @return whether encoding is successful.
   */
  bool createPayload(const MetadataMapVector& metadata_map_vector);

  /**
   * @return true if there is payload left to be packed.
   */
  bool hasNextFrame();

  /**
   * Creates the metadata frame payload for the next metadata frame.
   * @param buf is the pointer to the destination memory where the payload should be copied to. len
   * is the largest length the memory can hold.
   * @return the size of frame payload, or -1 for failure.
   */
  ssize_t packNextFramePayload(uint8_t* buf, const size_t len);

  /**
   * Returns a vector denoting the sequence of METADATA frames that this encoder expects to pack,
   * and the flags to be set in each frame. This counts only frames that the encoder has not already
   * packed; to get the full sequence of frames corresponding to the metadata map vector, call this
   * before submitting any frames to nghttp2.
   * @return A vector indicating the header byte in each METADATA frame, in sequence.
   */
  std::vector<uint8_t> payloadFrameFlagBytes();

private:
  /**
   * Creates wire format HTTP/2 header block from metadata_map.
   * @param metadata_map supplies METADATA to encode.
   * @return whether encoding is successful.
   */
  bool createPayloadMetadataMap(const MetadataMap& metadata_map);

  /**
   * Creates wired format header blocks using nghttp2.
   * @param metadata_map supplies METADATA to encode.
   * @return true if the creation succeeds.
   */
  bool createHeaderBlockUsingNghttp2(const MetadataMap& metadata_map);

  // The METADATA payload to be sent.
  Buffer::OwnedImpl payload_;

  // Max payload size bound.
  const uint64_t max_payload_size_bound_ = 1024 * 1024;

  // Default HPACK table size.
  const size_t header_table_size_ = 4096;

  // TODO(soya3129): share deflater among all encoders in the same connection. The benefit is less
  // memory, and the caveat is encoding error on one stream can impact other streams.
  using Deflater = CSmartPtr<nghttp2_hd_deflater, nghttp2_hd_deflate_del>;
  Deflater deflater_;

  // Stores the remaining payload size of each metadata_map to be packed. The payload size is needed
  // so that we know where to delineate between different metadata_maps in the payload_ buffer. The
  // payload size gets updated when the payload is packed into metadata frames.
  std::deque<uint64_t> payload_size_queue_;
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
