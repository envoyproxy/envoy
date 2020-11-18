#include "extensions/filters/http/kill_request/kill_request_filter.h"

#include "extensions/filters/common/fault/fault_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KillRequest {

bool KillRequestFilter::IsKillRequestEnabled() {
  uint32_t denominator = 100;
  if (kill_request_.probability().denominator() ==
      envoy::type::v3::FractionalPercent::TEN_THOUSAND) {
    denominator = 10000;
  } else if (kill_request_.probability().denominator() ==
             envoy::type::v3::FractionalPercent::MILLION) {
    denominator = 1000000;
  }

  return (random_generator_.random() % denominator) < kill_request_.probability().numerator();
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

  if (is_kill_request && IsKillRequestEnabled()) {
    // Crash Envoy.
    raise(SIGABRT);
  }

  return Http::FilterHeadersStatus::Continue;
}

} // namespace KillRequest
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
