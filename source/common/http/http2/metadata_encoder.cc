#include "common/http/http2/metadata_encoder.h"

#include "nghttp2/nghttp2.h"

namespace Envoy {
namespace Http {
namespace Http2 {

bool MetadataEncoder::createPayload(MetadataMap& metadata_map) {
  if (payload_.length() > 0 || metadata_map.empty()) {
    // |payload_| is not empty or |metadata_map| is empty. Return failure.
    ENVOY_LOG(error, "payload_ size is {}, metadata_map is {}empty", payload_.length(),
              metadata_map.empty() ? "" : "not ");
    return false;
  }

  bool success = createHeaderBlockUsingNghttp2(metadata_map);
  if (!success || !hasNextFrame()) {
    ENVOY_LOG(error, "Failed to create payload.");
    return false;
  }
  return true;
}

bool MetadataEncoder::createHeaderBlockUsingNghttp2(MetadataMap& metadata_map) {
  // Constructs input for nghttp2 deflater (encoder). Encoding method used is
  // "HPACK Literal Header Field Never Indexed".
  const size_t nvlen = metadata_map.size();
  nghttp2_nv nva[nvlen];
  size_t i = 0;
  for (const auto& header : metadata_map) {
    nva[i++] = {const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(header.first.data())),
                const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(header.second.data())),
                header.first.size(), header.second.size(), NGHTTP2_NV_FLAG_NO_INDEX};
  }

  // Create deflater (encoder).
  // TODO(soya3129): share deflater among all encoders in the same connection.
  int rv;
  nghttp2_hd_deflater* deflater;
  rv = nghttp2_hd_deflate_new(&deflater, header_table_size_);
  if (rv != 0) {
    ENVOY_LOG(error, "nghttp2_hd_deflate_init failed.");
    nghttp2_hd_deflate_del(deflater);
    return false;
  }

  // Estimates the upper bound of output payload.
  size_t buflen = nghttp2_hd_deflate_bound(deflater, nva, nvlen);
  if (buflen > max_payload_size_bound_) {
    ENVOY_LOG(error, "Payload size {} exceeds the max bound.", buflen);
    return false;
  }
  Buffer::RawSlice iovec;
  payload_.reserve(buflen, &iovec, 1);
  if (iovec.len_ < buflen) {
    ENVOY_LOG(error, "Failed to reserve memory in |payload_|.");
    nghttp2_hd_deflate_del(deflater);
    return false;
  }
  iovec.len_ = buflen;

  // Creates payload using nghttp2.
  uint8_t* buf = reinterpret_cast<uint8_t*>(iovec.mem_);
  ssize_t result = nghttp2_hd_deflate_hd(deflater, buf, buflen, nva, nvlen);
  if (result < 0) {
    ENVOY_LOG(error, "Failed deflating.");
    nghttp2_hd_deflate_del(deflater);
    return false;
  }
  payload_.commit(&iovec, 1);

  nghttp2_hd_deflate_del(deflater);
  return true;
}

void MetadataEncoder::releasePayload(uint64_t len) { payload_.drain(len); }

bool MetadataEncoder::hasNextFrame() { return payload_.length() > 0; }

} // namespace Http2
} // namespace Http
} // namespace Envoy
