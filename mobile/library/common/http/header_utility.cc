#include "library/common/http/header_utility.h"

#include "source/common/http/header_map_impl.h"
#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"

#include "library/common/data/utility.h"

namespace Envoy {
namespace Http {
namespace Utility {

void toEnvoyHeaders(HeaderMap& transformed_headers, envoy_headers headers) {
  Envoy::Http::StatefulHeaderKeyFormatter& formatter = transformed_headers.formatter().value();
  for (envoy_map_size_t i = 0; i < headers.length; i++) {
    std::string key = Data::Utility::copyToString(headers.entries[i].key);
    // Make sure the formatter knows the original case.
    formatter.processKey(key);
    transformed_headers.addCopy(LowerCaseString(key),
                                Data::Utility::copyToString(headers.entries[i].value));
  }
  // The C envoy_headers struct can be released now because the headers have been copied.
  release_envoy_headers(headers);
}

RequestHeaderMapPtr toRequestHeaders(envoy_headers headers) {
  auto transformed_headers = RequestHeaderMapImpl::create();
  transformed_headers->setFormatter(
      std::make_unique<
          Extensions::Http::HeaderFormatters::PreserveCase::PreserveCaseHeaderFormatter>(
          false, envoy::extensions::http::header_formatters::preserve_case::v3::
                     PreserveCaseFormatterConfig::DEFAULT));
  toEnvoyHeaders(*transformed_headers, headers);
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

envoy_headers toBridgeHeaders(const HeaderMap& header_map, absl::string_view alpn) {
  int alpn_entry = alpn.empty() ? 0 : 1;
  envoy_map_entry* headers = static_cast<envoy_map_entry*>(
      safe_malloc(sizeof(envoy_map_entry) * (header_map.size() + alpn_entry)));
  envoy_headers transformed_headers;
  transformed_headers.length = 0;
  transformed_headers.entries = headers;

  header_map.iterate(
      [&transformed_headers, &header_map](const HeaderEntry& header) -> HeaderMap::Iterate {
        std::string key_val = std::string(header.key().getStringView());
        if (header_map.formatter().has_value()) {
          const Envoy::Http::StatefulHeaderKeyFormatter& formatter = header_map.formatter().value();
          key_val = formatter.format(key_val);
        }
        envoy_data key = Data::Utility::copyToBridgeData(key_val);
        envoy_data value = Data::Utility::copyToBridgeData(header.value().getStringView());

        transformed_headers.entries[transformed_headers.length] = {key, value};
        transformed_headers.length++;

        return HeaderMap::Iterate::Continue;
      });
  if (!alpn.empty()) {
    envoy_data key = Data::Utility::copyToBridgeData("x-envoy-upstream-alpn");
    envoy_data value = Data::Utility::copyToBridgeData(alpn);
    transformed_headers.entries[transformed_headers.length] = {key, value};
    transformed_headers.length++;
  }
  return transformed_headers;
}

} // namespace Utility
} // namespace Http
} // namespace Envoy
