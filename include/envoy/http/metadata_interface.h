#pragma once

#include <unordered_map>

namespace Envoy {
namespace Http {

/**
 * Please refer to #2394 for more info about Envoy METADATA.
 * Envoy metadata docs can be found at source/docs/h2_metadata.md.
 */
const uint8_t METADATA_FRAME_TYPE = 0x4d;
const uint8_t END_METADATA_FLAG = 0x4;

// NGHTTP2_MAX_PAYLOADLEN in nghttp2.
// TODO(soya3129): Respect max_frame_size after nghttp2 #1250 is resolved.
const uint64_t METADATA_MAX_PAYLOAD_SIZE = 16384;

typedef std::unordered_map<std::string, std::string> MetadataMap;
typedef std::unique_ptr<MetadataMap> MetadataMapPtr;

typedef std::function<void(std::unique_ptr<MetadataMap>)> MetadataCallback;

} // namespace Http
} // namespace Envoy
