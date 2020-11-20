#include "extensions/filters/http/kill_request/kill_request_filter.h"

#include <csignal>

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KillRequest {

bool KillRequestFilter::isKillRequestEnabled() {
  return ProtobufPercentHelper::evaluateFractionalPercent(kill_request_.probability(),
                                                          random_generator_.random());
}

Http::FilterHeadersStatus KillRequestFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  const auto kill_request_header = headers.get(KillRequestHeaders::get().KillRequest);
  bool is_kill_request = false;
  // This is an implicitly untrusted header, so per the API documentation only
  // the first value is used.
  if (kill_request_header.empty() ||
      !absl::SimpleAtob(kill_request_header[0]->value().getStringView(), &is_kill_request)) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (is_kill_request && isKillRequestEnabled()) {
    // Crash Envoy.
    raise(SIGABRT);
  }

  return Http::FilterHeadersStatus::Continue;
}

} // namespace KillRequest
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
