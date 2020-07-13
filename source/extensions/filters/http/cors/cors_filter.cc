#include "extensions/filters/http/cors/cors_filter.h"

#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/scope.h"

#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {

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

CorsFilterConfig::CorsFilterConfig(const std::string& stats_prefix, Stats::Scope& scope)
    : stats_(generateStats(stats_prefix + "cors.", scope)) {}

CorsFilter::CorsFilter(CorsFilterConfigSharedPtr config)
    : policies_({{nullptr, nullptr}}), config_(std::move(config)) {}

// This handles the CORS preflight request as described in
// https://www.w3.org/TR/cors/#resource-preflight-requests
Http::FilterHeadersStatus CorsFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  if (decoder_callbacks_->route() == nullptr ||
      decoder_callbacks_->route()->routeEntry() == nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }

  policies_ = {{
      decoder_callbacks_->route()->routeEntry()->corsPolicy(),
      decoder_callbacks_->route()->routeEntry()->virtualHost().corsPolicy(),
  }};

  if (!enabled() && !shadowEnabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  origin_ = headers.getInline(origin_handle.handle());
  if (origin_ == nullptr || origin_->value().empty()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (!isOriginAllowed(origin_->value())) {
    config_->stats().origin_invalid_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  config_->stats().origin_valid_.inc();
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

  auto response_headers{Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
      {{Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::OK))}})};

  response_headers->setInline(access_control_allow_origin_handle.handle(),
                              origin_->value().getStringView());

  if (allowCredentials()) {
    response_headers->setReferenceInline(access_control_allow_credentials_handle.handle(),
                                         Http::CustomHeaders::get().CORSValues.True);
  }

  if (!allowMethods().empty()) {
    response_headers->setInline(access_control_allow_methods_handle.handle(), allowMethods());
  }

  if (!allowHeaders().empty()) {
    response_headers->setInline(access_control_allow_headers_handle.handle(), allowHeaders());
  }

  if (!maxAge().empty()) {
    response_headers->setInline(access_control_max_age_handle.handle(), maxAge());
  }

  decoder_callbacks_->encodeHeaders(std::move(response_headers), true);

  return Http::FilterHeadersStatus::StopIteration;
}

// This handles simple CORS requests as described in
// https://www.w3.org/TR/cors/#resource-requests
Http::FilterHeadersStatus CorsFilter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  if (!is_cors_request_) {
    return Http::FilterHeadersStatus::Continue;
  }

  headers.setInline(access_control_allow_origin_handle.handle(), origin_->value().getStringView());
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
  const auto allow_origins = allowOrigins();
  if (allow_origins == nullptr) {
    return false;
  }
  for (const auto& allow_origin : *allow_origins) {
    if (allow_origin->match("*") || allow_origin->match(origin.getStringView())) {
      return true;
    }
  }
  return false;
}

const std::vector<Matchers::StringMatcherPtr>* CorsFilter::allowOrigins() {
  for (const auto policy : policies_) {
    if (policy && !policy->allowOrigins().empty()) {
      return &policy->allowOrigins();
    }
  }
  return nullptr;
}

const std::string& CorsFilter::allowMethods() {
  for (const auto policy : policies_) {
    if (policy && !policy->allowMethods().empty()) {
      return policy->allowMethods();
    }
  }
  return EMPTY_STRING;
}

const std::string& CorsFilter::allowHeaders() {
  for (const auto policy : policies_) {
    if (policy && !policy->allowHeaders().empty()) {
      return policy->allowHeaders();
    }
  }
  return EMPTY_STRING;
}

const std::string& CorsFilter::exposeHeaders() {
  for (const auto policy : policies_) {
    if (policy && !policy->exposeHeaders().empty()) {
      return policy->exposeHeaders();
    }
  }
  return EMPTY_STRING;
}

const std::string& CorsFilter::maxAge() {
  for (const auto policy : policies_) {
    if (policy && !policy->maxAge().empty()) {
      return policy->maxAge();
    }
  }
  return EMPTY_STRING;
}

bool CorsFilter::allowCredentials() {
  for (const auto policy : policies_) {
    if (policy && policy->allowCredentials()) {
      return policy->allowCredentials().value();
    }
  }
  return false;
}

bool CorsFilter::shadowEnabled() {
  for (const auto policy : policies_) {
    if (policy) {
      return policy->shadowEnabled();
    }
  }
  return false;
}

bool CorsFilter::enabled() {
  for (const auto policy : policies_) {
    if (policy) {
      return policy->enabled();
    }
  }
  return false;
}

} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
