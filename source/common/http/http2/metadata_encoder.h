#pragma once

#include <cstdint>
#include <queue>
#include <string>
#include <vector>

#include "envoy/http/codec.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/c_smart_ptr.h"
#include "source/common/common/logger.h"

#include "quiche/http2/adapter/data_source.h"
#include "quiche/http2/hpack/hpack_encoder.h"

namespace Envoy {
namespace Http {
namespace Http2 {

/**
 * A class that creates and sends METADATA payloads. A METADATA payload is a
 * group of string key value pairs encoded in HTTP/2 header blocks. METADATA
 * frames are constructed in two steps: first, the stream submits the
 * MetadataMapVector to the encoder, and later, the MetadataSources generate
 * frame payloads for transmission on the wire.
 */
class NewMetadataEncoder : Logger::Loggable<Logger::Id::http2> {
public:
  using MetadataSourceVector = std::vector<std::unique_ptr<http2::adapter::MetadataSource>>;
  NewMetadataEncoder();

  /**
   * Creates wire format HTTP/2 header block from a vector of metadata maps.
   * @param metadata_map_vector supplies the metadata map vector to encode.
   * @return whether encoding is successful.
   */
  MetadataSourceVector createSources(const MetadataMapVector& metadata_map_vector);

private:
  std::unique_ptr<http2::adapter::MetadataSource> createSource(const MetadataMap& metadata_map);

  spdy::HpackEncoder deflater_;
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
