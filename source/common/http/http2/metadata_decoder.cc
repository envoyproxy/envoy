#include "common/http/http2/metadata_decoder.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Http {
namespace Http2 {

MetadataDecoder::MetadataDecoder(uint64_t stream_id) : stream_id_(stream_id) {
  int rv = nghttp2_hd_inflate_new(&inflater_);
  ASSERT(rv == 0);
}

MetadataDecoder::~MetadataDecoder() {
  nghttp2_hd_inflate_del(inflater_);
}

void MetadataDecoder::receiveMetadata(const uint8_t* data, size_t len) {
  ENVOY_LOG(error, "++++++++++ receiveMetadata: ");
  if (data == nullptr || len == 0) {
    ENVOY_LOG(error, "No payload for the decoder to receive.");
    return;
  }

  Buffer::RawSlice iovec;
  payload_.reserve(len, &iovec, 1);
  ASSERT(iovec.len_ >= len);
  iovec.len_ = len;
  memcpy(iovec.mem_, data, len);
  payload_.commit(&iovec, 1);
}

bool MetadataDecoder::OnMetadataFrameComplete(bool end_metadata) {
  DecodeMetadataPayloadUsingNghttp2(end_metadata);

  if (end_metadata) {
    ENVOY_LOG(error, "++++++++++ end_metadata is {}", end_metadata);
    if (callback_ != nullptr) {
      callback_(metadata_map_);
      metadata_map_.clear();
    } else {
      metadata_map_list_.emplace_back(std::move(metadata_map_));
      ASSERT(metadata_map_.empty());
    }
  }
  return true;
}

void MetadataDecoder::DecodeMetadataPayloadUsingNghttp2(bool end_metadata) {
  ENVOY_LOG(error, "++++++++++ DecodeMetadataPayloadUsingNghttp2 payload: {}", payload_.toString());

  // Computes how many slices are needed to get all the data out.
  const int num_slices = payload_.getRawSlices(nullptr, 0);;
  Buffer::RawSlice slices[num_slices];
  payload_.getRawSlices(slices, num_slices);

  ENVOY_LOG(error, "++++++++++ num_slices: {}", num_slices);
  ENVOY_LOG(error, "++++++++++ payload: {}", payload_.toString());
  ENVOY_LOG(error, "++++++++++ payload.length(): {}", payload_.length());
  // Decodes header block using nghttp2.
  ssize_t payload_size_consumed = 0;
  for (int i = 0; i < num_slices; i++) {
    nghttp2_nv nv;
    int inflate_flags = 0;
    auto slice = slices[i];
    int is_end = (i == num_slices - 1 && end_metadata) ? 1 : 0;
    ENVOY_LOG(error, "+++++++++ in for loop, slice.len_: {}", slice.len_);

    while (slice.len_ > 0) {
      ENVOY_LOG(error, "++++++++++ payload constructed from slices: {}, len_: {}",
                std::string(reinterpret_cast<char*>(slice.mem_), slice.len_), slice.len_);
      ssize_t result = nghttp2_hd_inflate_hd2(inflater_, &nv, &inflate_flags,
                                              reinterpret_cast<uint8_t*>(slice.mem_), slice.len_,
                                              is_end);
      ASSERT(result >= 0);

      slice.mem_ = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(slice.mem_) + result);
      slice.len_ -= result;
      payload_size_consumed += result;
      ASSERT(!(result == 0 && slice.len_ > 0));
      ENVOY_LOG(error, "++++++++++ result: {}", result);
      ENVOY_LOG(error, "++++++++++ payload.length(): {}", payload_.length());

      if (inflate_flags & NGHTTP2_HD_INFLATE_EMIT) {
        ENVOY_LOG(error, "++++++++std::string(reinterpret_cast<char*>(nv.name), nv.namelen): {}",
                  std::string(reinterpret_cast<char*>(nv.name), nv.namelen));
        ENVOY_LOG(error, "++++++++std::string(reinterpret_cast<char*>(nv.name), nv.namelen): {}",
                  std::string(reinterpret_cast<char*>(nv.value), nv.valuelen));
        metadata_map_.emplace(
            std::string(reinterpret_cast<char*>(nv.name), nv.namelen),
            std::string(reinterpret_cast<char*>(nv.value), nv.valuelen));
      }
    }

    if (inflate_flags & NGHTTP2_HD_INFLATE_FINAL) {
      ASSERT(end_metadata);
      nghttp2_hd_inflate_end_headers(inflater_);
    }
  }

  ENVOY_LOG(error, "++++++++++ metadata_map.size(): {}", metadata_map_.size());
  ENVOY_LOG(error, "++++++++++ consume metadata {}", payload_size_consumed);
  payload_.drain(payload_size_consumed);
  ENVOY_LOG(error, "++++++++++ metadata_map_list_.size(): {}", metadata_map_list_.size());
}

void MetadataDecoder::registerMetadataCallback(MetadataCallback callback) {
  if (callback == nullptr) {
    ENVOY_LOG(error, "Registered callback function is nullptr.");
    return;
  }
  callback_ = std::move(callback);
  for (const auto& metadata_map : metadata_map_list_) {
    ENVOY_LOG(error, "++++++++++ register with metadata has value");
    callback_(metadata_map);
  }
  metadata_map_list_.clear();
}

void MetadataDecoder::unregisterMetadataCallback() {
  callback_ = nullptr;
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
