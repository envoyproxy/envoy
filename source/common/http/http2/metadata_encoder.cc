#include "common/http/http2/metadata_encoder.h"

#include "common/common/assert.h"

#include "absl/container/fixed_array.h"
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

  payload_size_queue_.push_back(payload_size_after - payload_size_before);
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
  absl::FixedArray<nghttp2_nv> nva(nvlen);
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
  auto reservation = payload_.reserveSingleSlice(buflen);
  ASSERT(reservation.slice().len_ >= buflen);

  // Creates payload using nghttp2.
  uint8_t* buf = reinterpret_cast<uint8_t*>(reservation.slice().mem_);
  const ssize_t result = nghttp2_hd_deflate_hd(deflater_.get(), buf, buflen, nva.begin(), nvlen);
  RELEASE_ASSERT(result > 0,
                 fmt::format("Failed to deflate metadata payload, with result {}.", result));

  reservation.commit(result);

  return true;
}

bool MetadataEncoder::hasNextFrame() { return payload_.length() > 0; }

ssize_t MetadataEncoder::packNextFramePayload(uint8_t* buf, const size_t len) {
  // If this RELEASE_ASSERT fires, nghttp2 has requested that we pack more
  // METADATA frames than we have payload to pack. This could mean that the
  // HTTP/2 codec has submitted too many METADATA frames to nghttp2, or it could
  // mean that nghttp2 has called its pack_extension_callback more than once per
  // METADATA frame the codec submitted.
  RELEASE_ASSERT(!payload_size_queue_.empty(),
                 "No payload remaining to pack into a METADATA frame.");
  const uint64_t current_payload_size =
      std::min(METADATA_MAX_PAYLOAD_SIZE, payload_size_queue_.front());

  // nghttp2 guarantees len is at least 16KiB. If the check fails, please verify
  // NGHTTP2_MAX_PAYLOADLEN is consistent with METADATA_MAX_PAYLOAD_SIZE.
  RELEASE_ASSERT(len >= current_payload_size,
                 fmt::format("METADATA payload buffer is too small ({}, expected at least {}).",
                             len, METADATA_MAX_PAYLOAD_SIZE));

  // Copies payload to the destination memory.
  payload_.copyOut(0, current_payload_size, buf);

  // Updates the remaining size of the current metadata_map. If no data left, removes the size entry
  // from the queue.
  payload_size_queue_.front() -= current_payload_size;
  if (payload_size_queue_.front() == 0) {
    payload_size_queue_.pop_front();
  }

  // Releases the payload that has been copied out.
  payload_.drain(current_payload_size);

  return current_payload_size;
}

std::vector<uint8_t> MetadataEncoder::payloadFrameFlagBytes() {
  std::vector<uint8_t> flags;
  flags.reserve(payload_size_queue_.size());
  for (uint64_t payload_size : payload_size_queue_) {
    uint64_t frame_count = payload_size / METADATA_MAX_PAYLOAD_SIZE +
                           ((payload_size % METADATA_MAX_PAYLOAD_SIZE) == 0 ? 0 : 1);
    for (uint64_t i = 0; i < frame_count - 1; ++i) {
      flags.push_back(0);
    }
    flags.push_back(END_METADATA_FLAG);
  }
  return flags;
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
