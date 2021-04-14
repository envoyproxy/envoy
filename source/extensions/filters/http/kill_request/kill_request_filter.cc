#include "extensions/filters/http/kill_request/kill_request_filter.h"

#include <csignal>
#include <string>

#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.h"
#include "envoy/http/header_map.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KillRequest {

using ::envoy::extensions::filters::http::kill_request::v3::KillRequest;

KillSettings::KillSettings(const KillRequest& kill_request)
    : kill_probability_(kill_request.probability()), direction_(kill_request.direction()) {}

bool KillRequestFilter::isKillRequestEnabled() {
  return ProtobufPercentHelper::evaluateFractionalPercent(kill_request_.probability(),
                                                          random_generator_.random());
}

bool KillRequestFilter::isKillRequest(Http::HeaderMap& headers) {
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
    return false;
  }

  return is_kill_request;
}

Http::FilterHeadersStatus KillRequestFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  bool is_correct_direction = kill_request_.direction() == KillRequest::REQUEST;
  const bool is_kill_request = isKillRequest(headers);
  if (!is_kill_request) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Route-level configuration overrides filter-level configuration.
  if (decoder_callbacks_->route() && decoder_callbacks_->route()->routeEntry()) {
    const std::string& name = Extensions::HttpFilters::HttpFilterNames::get().KillRequest;
    const auto* route_entry = decoder_callbacks_->route()->routeEntry();

    const auto* per_route_kill_settings =
        route_entry->mostSpecificPerFilterConfigTyped<KillSettings>(name);

    if (per_route_kill_settings) {
      is_correct_direction = per_route_kill_settings->getDirection() == KillRequest::REQUEST;
      envoy::type::v3::FractionalPercent probability = per_route_kill_settings->getProbability();
      kill_request_.set_allocated_probability(&probability);
    }
  }

  if (is_kill_request && is_correct_direction && isKillRequestEnabled()) {
    // Crash Envoy.
    raise(SIGABRT);
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus KillRequestFilter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  if (kill_request_.direction() == KillRequest::REQUEST) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (isKillRequest(headers) && isKillRequestEnabled()) {
    // Crash Envoy.
    raise(SIGABRT);
  }

  return Http::FilterHeadersStatus::Continue;
}

} // namespace KillRequest
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
