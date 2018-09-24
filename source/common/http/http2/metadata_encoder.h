#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Http {
namespace Http2 {

// TODO(yasong): Move those definitions to MetadataHandler.
const uint8_t METADATA_FRAME_TYPE = 0x4d;
const uint8_t END_METADATA_FLAG = 0x4;

typedef std::unordered_map<std::string, std::string> MetadataMap;

// A class that creates and sends METADATA payload. The METADATA payload is a
// group of string key value pairs encoded in HTTP/2 header block.
class MetadataEncoder : Logger::Loggable<Logger::Id::metadata> {
public:
  MetadataEncoder(uint64_t stream_id) : stream_id_(stream_id) {}

  // Creates wired format HTTP/2 header block from |metadata_map|. Returns true
  // if success. Only after previous payload is drained, new payload can be
  // encoded. This limitation is caused by nghttp2 can't incrementally encode
  // header block.
  bool createPayload(MetadataMap& metadata_map);

  // Called in nghttp2 callback function after METADATA is submitted. The function releases |len|
  // data in |payload_|.
  void releasePayload(uint64_t len);

  // Returns true if there is payload to be submitted.
  bool hasNextFrame();

  // Submit METADATA frame to nghttp2.
  int submitMetadata() {
    // TODO(yasong): Implement this in MetadataHandler.
    // metadata_handler_->submitMetadataFrame(stream_id_);
    ENVOY_LOG(error, "Submit METADATA for stream_id: {}.", stream_id_);
    return 0;
  }

  // Returns payload to be submitted.
  Buffer::OwnedImpl* payload() { return &payload_; }

  // TODO(yasong): move those functions to MetadataHandler, since it's session level parameter.
  // Set max payload size for METADATA frame.
  void setMaxMetadataSize(size_t size) { max_size_ = size; }
  // Get max payload size for METADATA frame.
  uint64_t getMaxMetadataSize() { return max_size_; }

private:
  // Creates wired format header blocks using nghttp2. Returns true if the creation succeeds.
  bool createHeaderBlockUsingNghttp2(MetadataMap& metadata_map);

  // The METADATA payload to be sent.
  Buffer::OwnedImpl payload_;

  // Max payload size for METADATA frame.
  uint64_t max_size_ = 16384;

  // The stream id the encoder is associated with.
  const uint64_t stream_id_;

  // Default HPACK table size.
  const size_t header_table_size_ = 4096;
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
