#include "bridge_utility.h"

#include <sstream>

#include "library/common/data/utility.h"

namespace Envoy {
namespace Platform {

envoy_headers rawHeaderMapAsEnvoyHeaders(const RawHeaderMap& headers) {
  size_t header_count = 0;
  for (const auto& pair : headers) {
    header_count += pair.second.size();
  }

  envoy_map_entry* headers_list =
      static_cast<envoy_map_entry*>(safe_malloc(sizeof(envoy_map_entry) * header_count));

  size_t i = 0;
  for (const auto& pair : headers) {
    const auto& key = pair.first;
    for (const auto& value : pair.second) {
      envoy_map_entry& header = headers_list[i++];
      header.key = Data::Utility::copyToBridgeData(key);
      header.value = Data::Utility::copyToBridgeData(value);
    }
  }

  envoy_headers raw_headers{
      .length = static_cast<envoy_map_size_t>(header_count),
      .entries = headers_list,
  };
  return raw_headers;
}

RawHeaderMap envoyHeadersAsRawHeaderMap(envoy_headers raw_headers) {
  RawHeaderMap headers;
  for (auto i = 0; i < raw_headers.length; i++) {
    auto key = Data::Utility::copyToString(raw_headers.entries[i].key);
    auto value = Data::Utility::copyToString(raw_headers.entries[i].value);

    if (!headers.contains(key)) {
      headers.emplace(key, std::vector<std::string>());
    }
    headers[key].push_back(value);
  }
  // free instead of release_envoy_headers
  // because we already free each envoy_data piecewise
  // during calls to envoy_data_as_string
  free(raw_headers.entries);
  return headers;
}

} // namespace Platform
} // namespace Envoy
