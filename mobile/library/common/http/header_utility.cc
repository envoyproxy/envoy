#include "library/common/http/header_utility.h"

#include "common/http/header_map_impl.h"

namespace Envoy {
namespace Http {
namespace Utility {

static inline envoy_data copyEnvoyData(size_t length, const uint8_t* source) {
  uint8_t* destination = static_cast<uint8_t*>(malloc(sizeof(uint8_t) * length));
  memcpy(destination, source, length);
  return {length, destination, nullptr, nullptr};
}

std::string convertToString(envoy_data s) {
  return std::string(reinterpret_cast<char*>(const_cast<uint8_t*>(s.bytes)), s.length);
}

HeaderMapPtr transformHeaders(envoy_headers headers) {
  Http::HeaderMapPtr transformed_headers = std::make_unique<HeaderMapImpl>();
  for (envoy_header_size_t i = 0; i < headers.length; i++) {
    transformed_headers->addCopy(LowerCaseString(convertToString(headers.headers[i].key)),
                                 convertToString(headers.headers[i].value));
  }
  return transformed_headers;
}

envoy_headers transformHeaders(const HeaderMap& header_map) {
  // TODO: provide utility for the caller to free allocated memory
  // https://github.com/lyft/envoy-mobile/issues/280
  envoy_header* headers =
      static_cast<envoy_header*>(malloc(sizeof(envoy_header) * header_map.size()));
  envoy_headers transformed_headers;
  transformed_headers.length = 0;
  transformed_headers.headers = headers;

  header_map.iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        envoy_headers* transformed_headers = static_cast<envoy_headers*>(context);

        const absl::string_view header_key = header.key().getStringView();
        const absl::string_view header_value = header.value().getStringView();

        envoy_data key =
            copyEnvoyData(header_key.size(), reinterpret_cast<const uint8_t*>(header_key.data()));
        envoy_data value = copyEnvoyData(header_value.size(),
                                         reinterpret_cast<const uint8_t*>(header_value.data()));

        transformed_headers->headers[transformed_headers->length] = {key, value};
        transformed_headers->length++;

        return HeaderMap::Iterate::Continue;
      },
      &transformed_headers);
  return transformed_headers;
}

} // namespace Utility
} // namespace Http
} // namespace Envoy
