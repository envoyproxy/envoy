#include "common/http/http2/metadata_decoder.h"

#include "nghttp2/nghttp2.h"

namespace Envoy {
namespace Http {
namespace Http2 {

bool MetadataDecoder::receiveMetadata(const uint8_t *data, size_t len, bool end_metadata) {
  ENVOY_LOG(error, "++++++++++ receiveMetadata: end_metadata {}.", end_metadata);
  // Stores received data to |payload_|.
  Buffer::OwnedImpl payload(data, len);
  if (payload_.length() == 0) {
    payload_.move(payload);
  } else {
    payload_.prepend(payload);
  }

  if (end_metadata) {
    bool result = OnCompleteMetadata();
    if (!result) {
      payload_.drain(payload_.length());
      //check++++++++++++++++++++ does it need to be a map list??
      metadata_map_list_.clear();
      ENVOY_LOG(error, "Failed to decode METADATA in |payload_|.");
      return false;
    }
    for (const auto& metadata_map : metadata_map_list_) {
      callback_(metadata_map);
    }
    metadata_map_list_.clear();
  }
  return true;
}

void MetadataDecoder::RegisterMetadataCallback(MetadataCallback callback) {
  if (callback_ == nullptr) {
    ENVOY_LOG(error, "Registered callback function is nullptr.");
  }
  callback_ = std::move(callback);
  for (const auto& metadata_map : metadata_map_list_) {
    callback_(metadata_map);
  }
  metadata_map_list_.clear();
}

void MetadataDecoder::UnregisterMetadataCallback() {
  callback_ = nullptr;
}

bool MetadataDecoder::OnCompleteMetadata() {
  ENVOY_LOG(error, "++++++++++ OnCompleteMetadata payload: {}", payload_.toString());
  // TODO(soya3129): share deflater among all encoders in the same connection.
  nghttp2_hd_inflater *inflater;
  int rv = nghttp2_hd_inflate_new(&inflater);
  if (rv != 0) {
    ENVOY_LOG(error, "nghttp2_hd_inflate_new failed.");
    nghttp2_hd_inflate_del(inflater);
    return false;
  }

  // Computes how many slices are needed to get all the data out.
  const int num_slices = payload_.getRawSlices(nullptr, 0);;
  Buffer::RawSlice slices[num_slices];
  payload_.getRawSlices(slices, num_slices);

  ENVOY_LOG(error, "++++++++++ num_slices: {}", num_slices);
  ENVOY_LOG(error, "++++++++++ payload: {}", payload_.toString());
  ENVOY_LOG(error, "++++++++++ payload.length(): {}", payload_.length());
  // Decodes header block.
  MetadataMap metadata_map;
  for (int i = 0; i < num_slices; i++) {
    nghttp2_nv nv;
    int inflate_flags = 0;
    auto slice = slices[i];
    int is_end = (i == num_slices) ? 1 : 0;
    ENVOY_LOG(error, "+++++++++ in for loop, slice.len_: {}", slice.len_);

    while (slice.len_ > 0) {
      //ssize_t result = nghttp2_hd_inflate_hd(inflater, &nv, &inflate_flags,
      //                                        reinterpret_cast<uint8_t*>(slice.mem_), slice.len_,
      //                                        is_end);
      ssize_t result = nghttp2_hd_inflate_hd(inflater, &nv, &inflate_flags,
                                              const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(payload_.toString().data())), slice.len_,
                                              is_end);
      if (result < 0 || (result == 0 && slice.len_ > 0)) {
        nghttp2_hd_inflate_del(inflater);
        ENVOY_LOG(error, "Inflate failed with result code: {}, and slice.len_ is {}.", result,
                  slice.len_);
        return false;
      }
      slice.mem_ = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(slice.mem_) + result);
      slice.len_ -= result;

      if (inflate_flags & NGHTTP2_HD_INFLATE_EMIT) {
        metadata_map[std::string(reinterpret_cast<char*>(nv.name), nv.namelen)] =
            std::string(reinterpret_cast<char*>(nv.value), nv.valuelen);
      }
    }

    if (inflate_flags & NGHTTP2_HD_INFLATE_FINAL) {
      nghttp2_hd_inflate_end_headers(inflater);
    }
  }

  if (callback_ != nullptr) {
    for (const auto& metadata_map : metadata_map_list_) {
      callback_(metadata_map);
    }
  }

  nghttp2_hd_inflate_del(inflater);
  return true;
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
