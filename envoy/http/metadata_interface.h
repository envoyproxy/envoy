#pragma once

#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include "absl/container/node_hash_map.h"
#include "absl/strings/escaping.h"
#include "fmt/format.h"
#include "fmt/ostream.h"

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

using UnorderedStringMap = absl::node_hash_map<std::string, std::string>;

class MetadataMap : public UnorderedStringMap {
public:
  using UnorderedStringMap::UnorderedStringMap;

  friend std::ostream& operator<<(std::ostream& out, const MetadataMap& metadata_map) {
    out << "metadata map:";
    for (const auto& metadata : metadata_map) {
      out << "\nkey: " << absl::CEscape(metadata.first)
          << ", value: " << absl::CEscape(metadata.second) << std::endl;
    }
    return out;
  }
};

using MetadataMapPtr = std::unique_ptr<MetadataMap>;

using VectorMetadataMapPtr = std::vector<MetadataMapPtr>;

class MetadataMapVector : public VectorMetadataMapPtr {
public:
  using VectorMetadataMapPtr::VectorMetadataMapPtr;

  friend std::ostream& operator<<(std::ostream& out, const MetadataMapVector& metadata_map_vector) {
    out << "metadata_map_vector:\n";
    for (const auto& metadata_map : metadata_map_vector) {
      out << *metadata_map;
    }
    return out;
  }
};

using MetadataCallback = std::function<void(MetadataMapPtr&&)>;

} // namespace Http
} // namespace Envoy

// NOLINT(namespace-envoy)
namespace fmt {

// Specialize printing Envoy::Http::MetadataMap as we need to escape possible
// invalid utf8 string contained in MetadataMap.
template <> struct formatter<Envoy::Http::MetadataMap> {
  template <typename ParseContext>
  FMT_CONSTEXPR auto parse(ParseContext& ctx) -> decltype(ctx.begin()) {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(const Envoy::Http::MetadataMap& map, FormatContext& ctx) const
      -> decltype(ctx.out()) {
    std::ostringstream out;
    out << map;
    auto str = out.str();
    return fmt::formatter<std::string>().format(str, ctx);
  }
};

template <> struct formatter<::Envoy::Http::MetadataMapVector> : fmt::ostream_formatter {};

} // namespace fmt
