#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {

// Legacy default value of 60K is safely under both codec default limits.
static constexpr uint32_t DEFAULT_MAX_REQUEST_HEADERS_KB = 60;
// Default maximum number of headers.
static constexpr uint32_t DEFAULT_MAX_HEADERS_COUNT = 100;

constexpr absl::string_view MaxRequestHeadersCountOverrideKey =
    "envoy.reloadable_features.max_request_headers_count";
constexpr absl::string_view MaxResponseHeadersCountOverrideKey =
    "envoy.reloadable_features.max_response_headers_count";
constexpr absl::string_view MaxRequestHeadersSizeOverrideKey =
    "envoy.reloadable_features.max_request_headers_size_kb";
constexpr absl::string_view MaxResponseHeadersSizeOverrideKey =
    "envoy.reloadable_features.max_response_headers_size_kb";

} // namespace Http
} // namespace Envoy
