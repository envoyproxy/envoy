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

// A class that decodes METADATA payload in the format of HTTP/2 header block into MetadataMap, a
// map of string key value pairs.
class MetadataDecoder : Logger::Loggable<Logger::Id::metadata> {
public:
  MetadataDecoder(uint64_t stream_id);
  ~MetadataDecoder();

  /**
   * Calls this function when METADATA frame payload is received. The payload doesn't need to be
   * complete.
   * @param data is the pointer to the start of the payload.
   * @param len is the size of the received payload.
   * @return whether Metadata is received successfully.
   */
  bool receiveMetadata(const uint8_t* data, size_t len);

  /**
   * Calls when a complete METADATA frame is received. The function will decode METADATA received.
   * If the frame is the last one in the group, the function triggers the registered callback
   * function callback_.
   * @param end_metadata indicates if all the METADATA has been received.
   * @return whether the operation succeeds.
   */
  bool onMetadataFrameComplete(bool end_metadata);

  /**
   * TODO(soya3129): Remove this function and metadata_map_list_ if callback is available before
   * construction.
   * Registers a callback to receive metadata events on the wire.
   * @param callback is the callback function.
   */
  void registerMetadataCallback(MetadataCallback callback);

  /**
   * Indicates that the caller is no longer interested in metadata events.
   */
  void unregisterMetadataCallback();

  /**
   * @return payload_.
   */
  Buffer::OwnedImpl& payload() { return payload_; }

  /**
   * @return stream_id.
   */
  uint64_t getStreamId() const { return stream_id_; }

private:
  /**
   * Decodes METADATA payload using nghttp2.
   * @param end_metadata indicates is END_METADATA is true.
   * @return if decoding succeeds.
   */
  bool decodeMetadataPayloadUsingNghttp2(bool end_metadata);

  // Metadata event callback function.
  MetadataCallback callback_;

  // Saved metadata in case callback function is not registered yet.
  // TODO(soya3129): consider removing this vector if it is not necessary in Envoy.
  std::vector<MetadataMap> metadata_map_list_;

  // Metadata that is currently under decoding.
  MetadataMap metadata_map_;

  // Payload received.
  Buffer::OwnedImpl payload_;

  // The stream id the decoder is associated with.
  const uint64_t stream_id_;

  // TODO(soya3129): consider sharing the inflater with all streams in a connection. Caveat:
  // inflater failure on one stream can impact other streams.
  nghttp2_hd_inflater* inflater_;

  // Payload size limit. If the payload received exceeds the limit, fails the connection.
  const uint64_t max_payload_size_bound_ = 1024 * 1024;
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
