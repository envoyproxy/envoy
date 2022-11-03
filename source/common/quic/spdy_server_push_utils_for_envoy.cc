#include "quiche/quic/core/http/spdy_server_push_utils.h"

// NOLINT(namespace-envoy)

// This file has a substitute definition for
// quiche/quic/core/http/spdy_server_push_utils.cc which depends on GURL.
// Since Envoy doesn't support server push, these functions shouldn't be
// executed at all.

using spdy::Http2HeaderBlock;

namespace quic {

// static
// NOLINTNEXTLINE(readability-identifier-naming)
std::string SpdyServerPushUtils::GetPromisedUrlFromHeaders(const Http2HeaderBlock& /*headers*/) {
  return "";
}

// static
std::string
// NOLINTNEXTLINE(readability-identifier-naming)
SpdyServerPushUtils::GetPromisedHostNameFromHeaders(const Http2HeaderBlock& /*headers*/) {
  return "";
}

// static
// NOLINTNEXTLINE(readability-identifier-naming)
bool SpdyServerPushUtils::PromisedUrlIsValid(const Http2HeaderBlock& /*headers*/) { return false; }

// static
// NOLINTNEXTLINE(readability-identifier-naming)
std::string SpdyServerPushUtils::GetPushPromiseUrl(absl::string_view /*scheme*/,
                                                   absl::string_view /*authority*/,
                                                   absl::string_view /*path*/) {
  return "";
}

} // namespace quic
