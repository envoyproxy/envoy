#include "common/http/http2/metadata_decoder.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Http {
namespace Http2 {

MetadataDecoder::MetadataDecoder(uint64_t stream_id, MetadataCallback cb) : stream_id_(stream_id) {
  nghttp2_hd_inflater* inflater;
  int rv = nghttp2_hd_inflate_new(&inflater);
  ASSERT(rv == 0);
  inflater_ = Inflater(inflater);

  ASSERT(cb != nullptr);
  callback_ = std::move(cb);
}

bool MetadataDecoder::receiveMetadata(const uint8_t* data, size_t len) {
  ASSERT(data != nullptr && len != 0);
  payload_.add(data, len);

  return payload_.length() <= max_payload_size_bound_;
}

bool MetadataDecoder::onMetadataFrameComplete(bool end_metadata) {
  bool success = decodeMetadataPayloadUsingNghttp2(end_metadata);
  if (!success) {
    return false;
  }

  if (end_metadata) {
    callback_(metadata_map_);
    metadata_map_.clear();
  }
  return true;
}

bool MetadataDecoder::decodeMetadataPayloadUsingNghttp2(bool end_metadata) {
  // Computes how many slices are needed to get all the data out.
  const int num_slices = payload_.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  payload_.getRawSlices(slices, num_slices);

  // Data consumed by nghttp2 so far.
  ssize_t payload_size_consumed = 0;
  // Decodes header block using nghttp2.
  for (int i = 0; i < num_slices; i++) {
    nghttp2_nv nv;
    int inflate_flags = 0;
    auto slice = slices[i];
    // is_end indicates if the data in slice is the last data in the current
    // header block.
    bool is_end = i == (num_slices - 1) && end_metadata;

    // Feeds data to nghttp2 to decode.
    while (slice.len_ > 0) {
      ssize_t result =
          nghttp2_hd_inflate_hd2(inflater_.get(), &nv, &inflate_flags,
                                 reinterpret_cast<uint8_t*>(slice.mem_), slice.len_, is_end);
      if (result < 0 || (result == 0 && slice.len_ > 0)) {
        // If decoding fails, or there is data left in slice, but no data can be consumed by
        // nghttp2, return false.
        ENVOY_LOG(error, "Failed to decode payload.");
        return false;
      }

      slice.mem_ = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(slice.mem_) + result);
      slice.len_ -= result;
      payload_size_consumed += result;

      if (inflate_flags & NGHTTP2_HD_INFLATE_EMIT) {
        // One header key value pair has been successfully decoded.
        metadata_map_.emplace(std::string(reinterpret_cast<char*>(nv.name), nv.namelen),
                              std::string(reinterpret_cast<char*>(nv.value), nv.valuelen));
      }
    }

    if (slice.len_ == 0 && is_end) {
      // After one header block is decoded, reset inflater.
      ASSERT(end_metadata);
      nghttp2_hd_inflate_end_headers(inflater_.get());
    }
  }

  payload_.drain(payload_size_consumed);
  return true;
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
