#include "source/extensions/filters/http/cors/cors_filter.h"

#include <algorithm>

#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/scope.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {

struct HttpResponseCodeDetailValues {
  const absl::string_view CorsResponse = "cors_response";
};
using HttpResponseCodeDetails = ConstSingleton<HttpResponseCodeDetailValues>;

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    access_control_request_headers_handle(Http::CustomHeaders::get().AccessControlRequestHeaders);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    access_control_request_method_handle(Http::CustomHeaders::get().AccessControlRequestMethod);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    origin_handle(Http::CustomHeaders::get().Origin);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    access_control_allow_origin_handle(Http::CustomHeaders::get().AccessControlAllowOrigin);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    access_control_allow_credentials_handle(
        Http::CustomHeaders::get().AccessControlAllowCredentials);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    access_control_allow_methods_handle(Http::CustomHeaders::get().AccessControlAllowMethods);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    access_control_allow_headers_handle(Http::CustomHeaders::get().AccessControlAllowHeaders);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    access_control_max_age_handle(Http::CustomHeaders::get().AccessControlMaxAge);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    access_control_expose_headers_handle(Http::CustomHeaders::get().AccessControlExposeHeaders);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    access_control_request_private_network_handle(
        Http::CustomHeaders::get().AccessControlRequestPrviateNetwork);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    access_control_response_private_network_handle(
        Http::CustomHeaders::get().AccessControlAllowPrviateNetwork);

CorsFilterConfig::CorsFilterConfig(const std::string& stats_prefix, Stats::Scope& scope)
    : stats_(generateStats(stats_prefix + "cors.", scope)) {}

CorsFilter::CorsFilter(CorsFilterConfigSharedPtr config) : config_(std::move(config)) {}

void CorsFilter::initializeCorsPolicies() {
  policies_ = Http::Utility::getAllPerFilterConfig<Router::CorsPolicy>(decoder_callbacks_);

  // The 'perFilterConfigs' will handle cors policy of virtual host first. So, we need
  // reverse the 'policies_' to make sure the cors policy of route entry to be first item in the
  // 'policies_'.
  if (policies_.size() >= 2) {
    std::reverse(policies_.begin(), policies_.end());
  }

  // If no cors policy is configured in the per filter config, then the cors policy fields in the
  // route configuration will be ignored.
  if (policies_.empty()) {
    const auto route = decoder_callbacks_->route();
    ASSERT(route != nullptr);
    ASSERT(route->routeEntry() != nullptr);

    if (auto* typed_cfg = route->routeEntry()->corsPolicy(); typed_cfg != nullptr) {
      policies_.push_back(*typed_cfg);
    }

    if (auto* typed_cfg = route->virtualHost().corsPolicy(); typed_cfg != nullptr) {
      policies_.push_back(*typed_cfg);
    }
  }
}

// This handles the CORS preflight request as described in
// https://www.w3.org/TR/cors/#resource-preflight-requests
Http::FilterHeadersStatus CorsFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  if (decoder_callbacks_->route() == nullptr ||
      decoder_callbacks_->route()->routeEntry() == nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }

  initializeCorsPolicies();

  if (!enabled() && !shadowEnabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  const Http::HeaderEntry* origin = headers.getInline(origin_handle.handle());
  if (origin == nullptr || origin->value().empty()) {
    return Http::FilterHeadersStatus::Continue;
  }

  const bool origin_allowed = isOriginAllowed(origin->value());
  if (!origin_allowed) {
    config_->stats().origin_invalid_.inc();
  } else {
    config_->stats().origin_valid_.inc();
    latched_origin_ = std::string(origin->value().getStringView());
  }

  if (shadowEnabled() && !enabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  is_cors_request_ = true;

  const absl::string_view method = headers.getMethodValue();
  if (method != Http::Headers::get().MethodValues.Options) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (headers.getInlineValue(access_control_request_method_handle.handle()).empty()) {
    return Http::FilterHeadersStatus::Continue;
  }

  // This is pre-flight request, as it fulfills the following requirements:
  // - method is OPTIONS
  // - "origin" header is not empty
  // - "Access-Control-Request-Method" is not empty"
  if (!origin_allowed && forwardNotMatchingPreflights()) {
    return Http::FilterHeadersStatus::Continue;
  }

  auto response_headers{Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
      {{Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::OK))}})};

  if (origin_allowed) {
    response_headers->setInline(access_control_allow_origin_handle.handle(),
                                origin->value().getStringView());
  }

  if (allowCredentials()) {
    response_headers->setReferenceInline(access_control_allow_credentials_handle.handle(),
                                         Http::CustomHeaders::get().CORSValues.True);
  }

  const absl::string_view allow_methods = allowMethods();
  if (!allow_methods.empty()) {
    if (allow_methods == "*") {
      response_headers->setInline(
          access_control_allow_methods_handle.handle(),
          headers.getInlineValue(access_control_request_method_handle.handle()));
    } else {
      response_headers->setInline(access_control_allow_methods_handle.handle(), allow_methods);
    }
  }

  const absl::string_view allow_headers = allowHeaders();
  if (!allow_headers.empty()) {
    if (allow_headers == "*") {
      response_headers->setInline(
          access_control_allow_headers_handle.handle(),
          headers.getInlineValue(access_control_request_headers_handle.handle()));
    } else {
      response_headers->setInline(access_control_allow_headers_handle.handle(), allow_headers);
    }
  }

  if (!maxAge().empty()) {
    response_headers->setInline(access_control_max_age_handle.handle(), maxAge());
  }

  // More details refer to https://developer.chrome.com/blog/private-network-access-preflight.
  if (allowPrivateNetworkAccess() &&
      headers.getInlineValue(access_control_request_private_network_handle.handle()) == "true") {
    response_headers->setInline(access_control_response_private_network_handle.handle(), "true");
  }

  decoder_callbacks_->encodeHeaders(std::move(response_headers), true,
                                    HttpResponseCodeDetails::get().CorsResponse);

  return Http::FilterHeadersStatus::StopIteration;
}

// This handles simple CORS requests as described in
// https://www.w3.org/TR/cors/#resource-requests
Http::FilterHeadersStatus CorsFilter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  if (!is_cors_request_) {
    return Http::FilterHeadersStatus::Continue;
  }
  // Origin did not match. Do not modify the response headers.
  if (latched_origin_.empty()) {
    return Http::FilterHeadersStatus::Continue;
  }

  headers.setInline(access_control_allow_origin_handle.handle(), latched_origin_);

  if (allowCredentials()) {
    headers.setReferenceInline(access_control_allow_credentials_handle.handle(),
                               Http::CustomHeaders::get().CORSValues.True);
  }

  if (!exposeHeaders().empty()) {
    headers.setInline(access_control_expose_headers_handle.handle(), exposeHeaders());
  }

  return Http::FilterHeadersStatus::Continue;
}

void CorsFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

bool CorsFilter::isOriginAllowed(const Http::HeaderString& origin) {
  for (const auto& allow_origin : allowOrigins()) {
    if (allow_origin->match("*") || allow_origin->match(origin.getStringView())) {
      return true;
    }
  }
  return false;
}

absl::Span<const Matchers::StringMatcherPtr> CorsFilter::allowOrigins() {
  for (const Router::CorsPolicy& policy : policies_) {
    if (!policy.allowOrigins().empty()) {
      return policy.allowOrigins();
    }
  }
  return {};
}

bool CorsFilter::forwardNotMatchingPreflights() {
  for (const Router::CorsPolicy& policy : policies_) {
    if (policy.forwardNotMatchingPreflights()) {
      return policy.forwardNotMatchingPreflights().value();
    }
  }
  return true;
}

absl::string_view CorsFilter::allowMethods() {
  for (const Router::CorsPolicy& policy : policies_) {
    if (!policy.allowMethods().empty()) {
      return policy.allowMethods();
    }
  }
  return EMPTY_STRING;
}

absl::string_view CorsFilter::allowHeaders() {
  for (const Router::CorsPolicy& policy : policies_) {
    if (!policy.allowHeaders().empty()) {
      return policy.allowHeaders();
    }
  }
  return EMPTY_STRING;
}

absl::string_view CorsFilter::exposeHeaders() {
  for (const Router::CorsPolicy& policy : policies_) {
    if (!policy.exposeHeaders().empty()) {
      return policy.exposeHeaders();
    }
  }
  return EMPTY_STRING;
}

absl::string_view CorsFilter::maxAge() {
  for (const Router::CorsPolicy& policy : policies_) {
    if (!policy.maxAge().empty()) {
      return policy.maxAge();
    }
  }
  return EMPTY_STRING;
}

bool CorsFilter::allowCredentials() {
  for (const Router::CorsPolicy& policy : policies_) {
    if (policy.allowCredentials()) {
      return policy.allowCredentials().value();
    }
  }
  return false;
}

bool CorsFilter::allowPrivateNetworkAccess() {
  for (const Router::CorsPolicy& policy : policies_) {
    if (policy.allowPrivateNetworkAccess()) {
      return policy.allowPrivateNetworkAccess().value();
    }
  }
  return false;
}

bool CorsFilter::shadowEnabled() {
  for (const Router::CorsPolicy& policy : policies_) {
    return policy.shadowEnabled();
  }
  return false;
}

bool CorsFilter::enabled() {
  for (const Router::CorsPolicy& policy : policies_) {
    return policy.enabled();
  }
  return false;
}

} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
