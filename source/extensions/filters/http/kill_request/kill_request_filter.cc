#include "extensions/filters/http/kill_request/kill_request_filter.h"

#include <csignal>
#include <string>

#include "common/protobuf/utility.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KillRequest {

using ::envoy::extensions::filters::http::kill_request::v3::KillRequest;

KillSettings::KillSettings(const KillRequest& kill_request)
    : kill_probability_(kill_request.probability()) {}

bool KillRequestFilter::isKillRequestEnabled() {
  return ProtobufPercentHelper::evaluateFractionalPercent(kill_request_.probability(),
                                                          random_generator_.random());
}

Http::FilterHeadersStatus KillRequestFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  // If not empty, configured kill header name will override the default header name.
  const Http::LowerCaseString kill_request_header_name =
      kill_request_.kill_request_header().empty()
          ? KillRequestHeaders::get().KillRequest
          : Http::LowerCaseString(kill_request_.kill_request_header());
  const auto kill_request_header = headers.get(kill_request_header_name);
  bool is_kill_request = false;
  // This is an implicitly untrusted header, so per the API documentation only
  // the first value is used.
  if (kill_request_header.empty() ||
      !absl::SimpleAtob(kill_request_header[0]->value().getStringView(), &is_kill_request)) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Route-level configuration overrides filter-level configuration.
  if (decoder_callbacks_->route() &&
      decoder_callbacks_->route()->routeEntry()) {
    const std::string& name =
        Extensions::HttpFilters::HttpFilterNames::get().KillRequest;
    const auto* route_entry = decoder_callbacks_->route()->routeEntry();

    const auto* per_route_kill_settings =
        route_entry->mostSpecificPerFilterConfigTyped<KillSettings>(name);

    if (per_route_kill_settings) {
      envoy::type::v3::FractionalPercent probability = per_route_kill_settings->getProbability();
      kill_request_.set_allocated_probability(&probability);
    }
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
