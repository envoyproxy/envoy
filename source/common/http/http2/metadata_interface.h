#pragma once

#include <unordered_map>

namespace Envoy {
namespace Http {
namespace Http2 {

/**
 * Please refer to #2394 or go/envoy-metadata for more info about Envoy METADATA.
 * TODO(soya3129): refer to Envoy document.
 */
const uint8_t METADATA_FRAME_TYPE = 0x4d;
const uint8_t END_METADATA_FLAG = 0x4;

typedef std::unordered_map<std::string, std::string> MetadataMap;

typedef std::function<void(const MetadataMap&)> MetadataCallback;

} // namespace Http2
} // namespace Http
} // namespace Envoy
