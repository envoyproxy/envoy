#include "source/common/http/http2/metadata_decoder.h"

#include "source/common/common/assert.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/container/fixed_array.h"
#include "quiche/http2/decoder/decode_buffer.h"
#include "quiche/http2/hpack/decoder/hpack_decoder.h"
#include "quiche/http2/hpack/decoder/hpack_decoder_listener.h"

namespace Envoy {
namespace Http {
namespace Http2 {
namespace {

class QuicheDecoderListener : public http2::HpackDecoderListener {
public:
  explicit QuicheDecoderListener(MetadataMap& map) : map_(map) {}

  // HpackDecoderListener
  void OnHeaderListStart() override {}
  void OnHeader(absl::string_view name, absl::string_view value) override {
    map_.emplace(name, value);
  }
  void OnHeaderListEnd() override {}
  void OnHeaderErrorDetected(absl::string_view error_message) override {
    ENVOY_LOG_MISC(error, "Failed to decode payload: {}", error_message);
    map_.clear();
  }

private:
  MetadataMap& map_;
};

} // anonymous namespace

// Since QuicheDecoderListener and http2::HpackDecoder are implementation details, this struct is
// defined here rather than the header file.
struct MetadataDecoder::HpackDecoderContext {
  HpackDecoderContext(MetadataMap& map, size_t max_payload_size_bound)
      : listener(map), decoder(&listener, max_payload_size_bound) {}
  QuicheDecoderListener listener;
  http2::HpackDecoder decoder;
};

MetadataDecoder::MetadataDecoder(MetadataCallback cb) {
  ASSERT(cb != nullptr);
  callback_ = std::move(cb);

  resetDecoderContext();
}

MetadataDecoder::~MetadataDecoder() = default;

bool MetadataDecoder::receiveMetadata(const uint8_t* data, size_t len) {
  ASSERT(data != nullptr && len != 0);
  payload_.add(data, len);

  total_payload_size_ += len;
  return total_payload_size_ <= max_payload_size_bound_;
}

bool MetadataDecoder::onMetadataFrameComplete(bool end_metadata) {
  const bool success = decodeMetadataPayload(end_metadata);
  if (!success) {
    return false;
  }

  if (end_metadata) {
    callback_(std::move(metadata_map_));
    resetDecoderContext();
  }
  return true;
}

bool MetadataDecoder::decodeMetadataPayload(bool end_metadata) {
  Buffer::RawSliceVector slices = payload_.getRawSlices();

  // Data consumed by the decoder so far.
  ssize_t payload_size_consumed = 0;
  for (const Buffer::RawSlice& slice : slices) {
    http2::DecodeBuffer db(static_cast<char*>(slice.mem_), slice.len_);
    while (db.HasData()) {
      if (!decoder_context_->decoder.DecodeFragment(&db)) {
        ENVOY_LOG_MISC(error, "Failed to decode payload: {}",
                       http2::HpackDecodingErrorToString(decoder_context_->decoder.error()));
        return false;
      }
    }
    payload_size_consumed += slice.len_;
  }
  if (end_metadata) {
    const bool result = decoder_context_->decoder.EndDecodingBlock();
    if (!result) {
      ENVOY_LOG_MISC(error, "Failed to decode payload: {}",
                     http2::HpackDecodingErrorToString(decoder_context_->decoder.error()));
      return false;
    }
  }
  payload_.drain(payload_size_consumed);
  return true;
}

void MetadataDecoder::resetDecoderContext() {
  metadata_map_ = std::make_unique<MetadataMap>();
  decoder_context_ = std::make_unique<HpackDecoderContext>(*metadata_map_, max_payload_size_bound_);
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
