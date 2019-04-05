#include "extensions/filters/http/csrf/csrf_filter.h"

#include "envoy/stats/scope.h"

#include "common/common/empty_string.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Csrf {

CsrfFilterConfig::CsrfFilterConfig(const envoy::config::filter::http::csrf::v2::CsrfPolicy& policy,
                                   const std::string& stats_prefix, Stats::Scope& scope,
                                   Runtime::Loader& runtime)
    : stats_(generateStats(stats_prefix, scope)), policy_(generatePolicy(policy, runtime)) {}

CsrfFilter::CsrfFilter(const CsrfFilterConfigSharedPtr config) : config_(config) {}

Http::FilterHeadersStatus CsrfFilter::decodeHeaders(Http::HeaderMap& headers, bool) {
  determinePolicy();

  if (!policy_->enabled() && !policy_->shadowEnabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (!modifyMethod(headers)) {
    return Http::FilterHeadersStatus::Continue;
  }

  bool isValid = true;
  const auto& sourceOrigin = sourceOriginValue(headers);
  if (sourceOrigin == EMPTY_STRING) {
    isValid = false;
    config_->stats().missing_source_origin_.inc();
  }

  const auto& targetOrigin = targetOriginValue(headers);
  if (sourceOrigin != targetOrigin) {
    isValid = false;
    config_->stats().request_invalid_.inc();
  }

  if (isValid == true) {
    config_->stats().request_valid_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  if (policy_->shadowEnabled() && !policy_->enabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  callbacks_->sendLocalReply(Http::Code::Forbidden, "Invalid origin", nullptr, absl::nullopt);
  return Http::FilterHeadersStatus::StopIteration;
}

bool CsrfFilter::modifyMethod(const Http::HeaderMap& headers) {
  const auto& method = headers.Method();
  if (method == nullptr) {
    return false;
  }
  const auto& method_type = method->value().c_str();
  return (method_type == Http::Headers::get().MethodValues.Post ||
          method_type == Http::Headers::get().MethodValues.Put ||
          method_type == Http::Headers::get().MethodValues.Delete);
}

absl::string_view CsrfFilter::sourceOriginValue(const Http::HeaderMap& headers) {
  const auto& origin = hostAndPort(headers.Origin());
  if (origin != EMPTY_STRING) {
    return origin;
  }
  return hostAndPort(headers.Referer());
}

absl::string_view CsrfFilter::targetOriginValue(const Http::HeaderMap& headers) {
  return hostAndPort(headers.Host());
}

absl::string_view CsrfFilter::hostAndPort(const Http::HeaderEntry* header) {
  Http::Utility::Url absolute_url;
  if (header != nullptr && !header->value().empty()) {
    if (absolute_url.initialize(header->value().getStringView())) {
      return absolute_url.host_and_port();
    }
    return header->value().getStringView();
  }
  return EMPTY_STRING;
}

void CsrfFilter::determinePolicy() {
  // Prioritize global config first.
  policy_ = config_->policy();
  // If the route has a policy use that.
  if (callbacks_->route() && callbacks_->route()->routeEntry()) {
    const std::string& name = Extensions::HttpFilters::HttpFilterNames::get().Csrf;
    const auto* route_entry = callbacks_->route()->routeEntry();

    const CsrfPolicy* route_policy = route_entry->perFilterConfigTyped<CsrfPolicy>(name);
    const CsrfPolicy* per_route_policy =
        route_policy ? route_policy
                     : route_entry->virtualHost().perFilterConfigTyped<CsrfPolicy>(name);
    policy_ = per_route_policy ? per_route_policy : policy_;
  }
}

} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
