#include "bridge_utility.h"

#include "library/common/data/utility.h"

namespace Envoy {
namespace Platform {

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
  // because we already free each envoy_data individually
  // during calls to envoy_data_as_string
  release_envoy_headers(raw_headers);
  return headers;
}

} // namespace Platform
} // namespace Envoy
