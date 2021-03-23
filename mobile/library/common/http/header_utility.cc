#include "library/common/http/header_utility.h"

#include "common/http/header_map_impl.h"

#include "library/common/data/utility.h"

namespace Envoy {
namespace Http {
namespace Utility {

RequestHeaderMapPtr toRequestHeaders(envoy_headers headers) {
  RequestHeaderMapPtr transformed_headers = RequestHeaderMapImpl::create();
  for (envoy_map_size_t i = 0; i < headers.length; i++) {
    transformed_headers->addCopy(
        LowerCaseString(Data::Utility::copyToString(headers.entries[i].key)),
        Data::Utility::copyToString(headers.entries[i].value));
  }
  // The C envoy_headers struct can be released now because the headers have been copied.
  release_envoy_headers(headers);
  return transformed_headers;
}

RequestTrailerMapPtr toRequestTrailers(envoy_headers trailers) {
  RequestTrailerMapPtr transformed_trailers = RequestTrailerMapImpl::create();
  for (envoy_map_size_t i = 0; i < trailers.length; i++) {
    transformed_trailers->addCopy(
        LowerCaseString(Data::Utility::copyToString(trailers.entries[i].key)),
        Data::Utility::copyToString(trailers.entries[i].value));
  }
  // The C envoy_headers struct can be released now because the headers have been copied.
  release_envoy_headers(trailers);
  return transformed_trailers;
}

envoy_headers toBridgeHeaders(const HeaderMap& header_map) {
  envoy_map_entry* headers =
      static_cast<envoy_map_entry*>(safe_malloc(sizeof(envoy_map_entry) * header_map.size()));
  envoy_headers transformed_headers;
  transformed_headers.length = 0;
  transformed_headers.entries = headers;

  header_map.iterate([&transformed_headers](const HeaderEntry& header) -> HeaderMap::Iterate {
    envoy_data key = Data::Utility::copyToBridgeData(header.key().getStringView());
    envoy_data value = Data::Utility::copyToBridgeData(header.value().getStringView());

    transformed_headers.entries[transformed_headers.length] = {key, value};
    transformed_headers.length++;

    return HeaderMap::Iterate::Continue;
  });
  return transformed_headers;
}

} // namespace Utility
} // namespace Http
} // namespace Envoy
