#include "library/common/http/header_utility.h"

#include "source/common/http/header_map_impl.h"
#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"

#include "library/common/bridge//utility.h"

namespace Envoy {
namespace Http {
namespace Utility {

Http::LocalErrorStatus statusForOnLocalReply(const StreamDecoderFilter::LocalReplyData&,
                                             const StreamInfo::StreamInfo&) {
  // Avoid sendLocalReply to differentiate Envoy generated errors from peer generated errors.
  return Http::LocalErrorStatus::ContinueAndResetStream;
}

void toEnvoyHeaders(HeaderMap& envoy_result_headers, envoy_headers headers) {
  Envoy::Http::StatefulHeaderKeyFormatter& formatter = envoy_result_headers.formatter().value();
  for (envoy_map_size_t i = 0; i < headers.length; i++) {
    std::string key = Bridge::Utility::copyToString(headers.entries[i].key);
    // Make sure the formatter knows the original case.
    formatter.processKey(key);
    envoy_result_headers.addCopy(LowerCaseString(key),
                                 Bridge::Utility::copyToString(headers.entries[i].value));
  }
  // The C envoy_headers struct can be released now because the headers have been copied.
  release_envoy_headers(headers);
}

RequestHeaderMapPtr toRequestHeaders(envoy_headers headers) {
  auto transformed_headers = createRequestHeaderMapPtr();
  toEnvoyHeaders(*transformed_headers, headers);
  return transformed_headers;
}

RequestHeaderMapPtr createRequestHeaderMapPtr() {
  auto transformed_headers = RequestHeaderMapImpl::create();
  transformed_headers->setFormatter(
      std::make_unique<
          Extensions::Http::HeaderFormatters::PreserveCase::PreserveCaseHeaderFormatter>(
          false, envoy::extensions::http::header_formatters::preserve_case::v3::
                     PreserveCaseFormatterConfig::DEFAULT));
  return transformed_headers;
}

RequestTrailerMapPtr createRequestTrailerMapPtr() { return RequestTrailerMapImpl::create(); }

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
        envoy_data key = Bridge::Utility::copyToBridgeData(key_val);
        envoy_data value = Bridge::Utility::copyToBridgeData(header.value().getStringView());

        transformed_headers.entries[transformed_headers.length] = {key, value};
        transformed_headers.length++;

        return HeaderMap::Iterate::Continue;
      });
  if (!alpn.empty()) {
    envoy_data key = Bridge::Utility::copyToBridgeData("x-envoy-upstream-alpn");
    envoy_data value = Bridge::Utility::copyToBridgeData(alpn);
    transformed_headers.entries[transformed_headers.length] = {key, value};
    transformed_headers.length++;
  }
  return transformed_headers;
}

} // namespace Utility
} // namespace Http
} // namespace Envoy
