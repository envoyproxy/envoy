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

class MetadataDecoder : Logger::Loggable<Logger::Id::metadata> {
public:
  MetadataDecoder(uint64_t stream_id);
  ~MetadataDecoder();

  /**
   * Calls this function when METADATA frame payload is received.
   * @param data is the pointer to the start of the payload.
   * @param len is the size of the payload.
   */
  void receiveMetadata(const uint8_t* data, size_t len);

  /**
   * Calls when a METADATA frame is received. The function will decode METADATA received. If the
   * frame is the last one in the group, triggers the callback functions.
   * @param end_metadata indicates if all the METADATA has been received.
   * @return whether the operation succeeds.
   */
  bool OnMetadataFrameComplete(bool end_metadata);

  /**
   * Registers a callback to receive metadata events received on the wire.
   * @param callback is the callback function.
   */
  void registerMetadataCallback(MetadataCallback callback);

  /**
   * Indicates that the caller is no longer interested in metadata events.
   */
  void unregisterMetadataCallback();

  std::vector<MetadataMap>& getMetadataMapList() { return metadata_map_list_; }

private:
  /**
   * Decodes METADATA payload. This function should be called after all the payload has been
   * received.
   */
  bool DecodeMetadataPayloadUsingNghttp2();

  // Metadata event callback function.
  MetadataCallback callback_;

  // Decoded metadata.
  std::vector<MetadataMap> metadata_map_list_;

  // Payload received.
  Buffer::OwnedImpl payload_;

  // The stream id the decoder is associated with.
  const uint64_t stream_id_;

  // Saves inflater before all METADATA frames are received.
  nghttp2_hd_inflater* inflater_;
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
