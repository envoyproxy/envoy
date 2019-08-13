#include "common/http/http2/metadata_encoder.h"

#include "common/common/assert.h"
#include "common/common/stack_array.h"

#include "nghttp2/nghttp2.h"

namespace Envoy {
namespace Http {
namespace Http2 {

MetadataEncoder::MetadataEncoder() {
  nghttp2_hd_deflater* deflater;
  const int rv = nghttp2_hd_deflate_new(&deflater, header_table_size_);
  ASSERT(rv == 0);
  deflater_ = Deflater(deflater);
}

bool MetadataEncoder::createPayloadMetadataMap(const MetadataMap& metadata_map) {
  ASSERT(!metadata_map.empty());

  const uint64_t payload_size_before = payload_.length();
  const bool success = createHeaderBlockUsingNghttp2(metadata_map);
  const uint64_t payload_size_after = payload_.length();

  if (!success || payload_size_after == payload_size_before) {
    ENVOY_LOG(error, "Failed to create payload.");
    return false;
  }

  payload_size_queue_.push(payload_size_after - payload_size_before);
  return true;
}

bool MetadataEncoder::createPayload(const MetadataMapVector& metadata_map_vector) {
  ASSERT(payload_.length() == 0);
  ASSERT(payload_size_queue_.empty());

  for (const auto& metadata_map : metadata_map_vector) {
    if (!createPayloadMetadataMap(*metadata_map)) {
      return false;
    }
  }
  return true;
}

bool MetadataEncoder::createHeaderBlockUsingNghttp2(const MetadataMap& metadata_map) {
  // Constructs input for nghttp2 deflater (encoder). Encoding method used is
  // "HPACK Literal Header Field Never Indexed".
  const size_t nvlen = metadata_map.size();
  STACK_ARRAY(nva, nghttp2_nv, nvlen);
  size_t i = 0;
  for (const auto& header : metadata_map) {
    nva[i++] = {const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(header.first.data())),
                const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(header.second.data())),
                header.first.size(), header.second.size(), NGHTTP2_NV_FLAG_NO_INDEX};
  }

  // Estimates the upper bound of output payload.
  const size_t buflen = nghttp2_hd_deflate_bound(deflater_.get(), nva.begin(), nvlen);
  if (buflen + payload_.length() > max_payload_size_bound_) {
    ENVOY_LOG(error, "Payload size {} exceeds the max bound.", buflen);
    return false;
  }
  Buffer::RawSlice iovec;
  payload_.reserve(buflen, &iovec, 1);
  ASSERT(iovec.len_ >= buflen);

  // Creates payload using nghttp2.
  uint8_t* buf = reinterpret_cast<uint8_t*>(iovec.mem_);
  const ssize_t result = nghttp2_hd_deflate_hd(deflater_.get(), buf, buflen, nva.begin(), nvlen);
  ASSERT(result > 0);
  iovec.len_ = result;

  payload_.commit(&iovec, 1);

  return true;
}

bool MetadataEncoder::hasNextFrame() { return payload_.length() > 0; }

uint64_t MetadataEncoder::packNextFramePayload(uint8_t* buf, const size_t len) {
  const uint64_t current_payload_size =
      std::min(METADATA_MAX_PAYLOAD_SIZE, payload_size_queue_.front());

  // nghttp2 guarantees len is at least 16KiB. If the check fails, please verify
  // NGHTTP2_MAX_PAYLOADLEN is consistent with METADATA_MAX_PAYLOAD_SIZE.
  ASSERT(len >= current_payload_size);

  // Copies payload to the destination memory.
  payload_.copyOut(0, current_payload_size, buf);

  // Updates the remaining size of the current metadata_map. If no data left, removes the size entry
  // from the queue.
  payload_size_queue_.front() -= current_payload_size;
  if (payload_size_queue_.front() == 0) {
    payload_size_queue_.pop();
  }

  // Releases the payload that has been copied out.
  payload_.drain(current_payload_size);

  return current_payload_size;
}

uint8_t MetadataEncoder::nextEndMetadata() {
  return payload_size_queue_.front() > METADATA_MAX_PAYLOAD_SIZE ? 0 : END_METADATA_FLAG;
}

uint64_t MetadataEncoder::frameCountUpperBound() {
  return payload_.length() / METADATA_MAX_PAYLOAD_SIZE + payload_size_queue_.size();
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
