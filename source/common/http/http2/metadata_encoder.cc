#include "source/common/http/http2/metadata_encoder.h"

#include "source/common/common/assert.h"

#include "absl/container/fixed_array.h"

namespace Envoy {
namespace Http {
namespace Http2 {

class BufferMetadataSource : public http2::adapter::MetadataSource {
public:
  explicit BufferMetadataSource(Buffer::OwnedImpl payload)
      : payload_(std::move(payload)), original_payload_length_(payload_.length()) {}

  size_t NumFrames(size_t max_frame_size) const override {
    // Rounds up, so a trailing partial frame is counted.
    return (original_payload_length_ + max_frame_size - 1) / max_frame_size;
  }

  std::pair<int64_t, bool> Pack(uint8_t* dest, size_t dest_len) override {
    const size_t to_copy = std::min(dest_len, static_cast<size_t>(payload_.length()));
    payload_.copyOut(0, to_copy, dest);
    payload_.drain(to_copy);
    return std::make_pair(static_cast<int64_t>(to_copy), payload_.length() == 0);
  }

  void OnFailure() override {}

private:
  Buffer::OwnedImpl payload_;
  const size_t original_payload_length_;
};

NewMetadataEncoder::NewMetadataEncoder() {
  deflater_.SetIndexingPolicy([](absl::string_view, absl::string_view) { return false; });
}

std::vector<std::unique_ptr<http2::adapter::MetadataSource>>
NewMetadataEncoder::createSources(const MetadataMapVector& metadata_map_vector) {
  MetadataSourceVector v;
  v.reserve(metadata_map_vector.size());
  for (const auto& metadata_map : metadata_map_vector) {
    v.push_back(createSource(*metadata_map));
  }
  return v;
}

std::unique_ptr<http2::adapter::MetadataSource>
NewMetadataEncoder::createSource(const MetadataMap& metadata_map) {
  static const size_t kMaxEncodingChunkSize = 64 * 1024;
  spdy::HpackEncoder::Representations r;
  r.reserve(metadata_map.size());
  for (const auto& header : metadata_map) {
    r.push_back({header.first, header.second});
  }
  ASSERT(r.size() == metadata_map.size());
  Buffer::OwnedImpl payload;
  auto progressive_encoder = deflater_.EncodeRepresentations(r);
  while (progressive_encoder->HasNext()) {
    payload.add(progressive_encoder->Next(kMaxEncodingChunkSize));
  }
  return std::make_unique<BufferMetadataSource>(std::move(payload));
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
