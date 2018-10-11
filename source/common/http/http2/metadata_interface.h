#pragma once

#include <unordered_map>

namespace Envoy {
namespace Http {
namespace Http2 {

/**
 * Please refer to #2394 for more info about Envoy METADATA.
 * TODO(soya3129): refer to Envoy document.
 *
 * METADATA frame is an Envoy specific extension for the standard HTTP/2 protocol. METADATA frame
 * can be used to communicate extra information related to a session or a stream. HTTP/2 protocol
 * provides an easy way for implementers to add new frame types. We define METADATA frame with type
 * 0x4d. The value is large enough that it is not likely to be used by any official extensions in
 * the near future. METADATA frame be used to communicate metadata between Envoy and any other peers
 * where METADATA frame type is supported. A proxy or peer may choose to consume METADATA frames,
 * pass them along unmodified, or modify the payloads. METADATA frames can be sent at any location
 * of a stream.
 */
const uint8_t METADATA_FRAME_TYPE = 0x4d;
const uint8_t END_METADATA_FLAG = 0x4;

typedef std::unordered_map<std::string, std::string> MetadataMap;

typedef std::function<void(const MetadataMap&)> MetadataCallback;

} // namespace Http2
} // namespace Http
} // namespace Envoy
