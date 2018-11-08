#pragma once

#include <functional>
#include <memory>
#include <unordered_map>

namespace Envoy {
namespace Http {

/**
 * Please refer to #2394 for more info about Envoy METADATA.
 * Envoy metadata docs can be found at source/docs/h2_metadata.md.
 */
constexpr uint8_t METADATA_FRAME_TYPE = 0x4d;
constexpr uint8_t END_METADATA_FLAG = 0x4;

// NGHTTP2_MAX_PAYLOADLEN in nghttp2.
// TODO(soya3129): Respect max_frame_size after nghttp2 #1250 is resolved.
constexpr uint64_t METADATA_MAX_PAYLOAD_SIZE = 16384;

using MetadataMap = std::unordered_map<std::string, std::string>;
using MetadataMapPtr = std::unique_ptr<MetadataMap>;

using MetadataCallback = std::function<void(std::unique_ptr<MetadataMap>)>;

} // namespace Http
} // namespace Envoy
