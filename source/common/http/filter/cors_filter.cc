#include "common/http/filter/cors_filter.h"

#include "envoy/http/codes.h"

#include "common/common/enum_to_int.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Http {

CorsFilter::CorsFilter(CorsFilterConfigConstSharedPtr config)
    : config_(config), is_cors_request_(false) {}

CorsFilter::~CorsFilter() {}

FilterHeadersStatus CorsFilter::decodeHeaders(HeaderMap& headers, bool) {
  origin_ = headers.Origin();
  if (origin_ == nullptr || origin_->value() == "") {
    return FilterHeadersStatus::Continue;
  }

  initialize();

  if (!enabled()) {
    return FilterHeadersStatus::Continue;
  }

  if (!isOriginAllowed(origin_->value())) {
    return FilterHeadersStatus::Continue;
  }

  is_cors_request_ = true;

  auto method = headers.Method();
  if (method == nullptr || method->value().c_str() != Http::Headers::get().MethodValues.Options) {
    return FilterHeadersStatus::Continue;
  }

  auto requestMethod = headers.AccessControlRequestMethod();
  if (requestMethod == nullptr || requestMethod->value() == "") {
    return FilterHeadersStatus::Continue;
  }

  HeaderMapPtr response_headers{
      new HeaderMapImpl{{Headers::get().Status, std::to_string(enumToInt(Http::Code::OK))}}};

  response_headers->insertAccessControlAllowOrigin().value(*origin_);

  if (allowCredentials()) {
    response_headers->insertAccessControlAllowCredentials().value(
        Http::Headers::get().CORSValues.True);
  }

  if (allowMethods() != "") {
    response_headers->insertAccessControlAllowMethods().value(allowMethods());
  }

  if (allowHeaders() != "") {
    response_headers->insertAccessControlAllowHeaders().value(allowHeaders());
  }

  if (exposeHeaders() != "") {
    response_headers->insertAccessControlExposeHeaders().value(exposeHeaders());
  }

  if (maxAge() != "") {
    response_headers->insertAccessControlMaxAge().value(maxAge());
  }

  decoder_callbacks_->encodeHeaders(std::move(response_headers), true);

  return FilterHeadersStatus::StopIteration;
}

FilterHeadersStatus CorsFilter::encodeHeaders(HeaderMap& headers, bool) {
  if (!is_cors_request_) {
    return FilterHeadersStatus::Continue;
  }

  headers.insertAccessControlAllowOrigin().value(*origin_);
  if (allowCredentials()) {
    headers.insertAccessControlAllowCredentials().value(Http::Headers::get().CORSValues.True);
  }

  return FilterHeadersStatus::Continue;
}

void CorsFilter::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
};

void CorsFilter::initialize() {
  route_cors_policy_ = decoder_callbacks_->route()->routeEntry()->corsPolicy();
  virtual_host_cors_policy_ = decoder_callbacks_->route()->routeEntry()->virtualHost().corsPolicy();
}

bool CorsFilter::isOriginAllowed(Http::HeaderString& origin) {
  for (const auto& o : allowOrigin()) {
    if (o == "*" || origin == o.c_str()) {
      return true;
    }
  }
  return false;
}

const std::list<std::string>& CorsFilter::allowOrigin() {
  if (!route_cors_policy_->allowOrigin().empty()) {
    return route_cors_policy_->allowOrigin();
  }
  return virtual_host_cors_policy_->allowOrigin();
}

const std::string& CorsFilter::allowMethods() {
  if (route_cors_policy_->allowMethods() != "") {
    return route_cors_policy_->allowMethods();
  }
  return virtual_host_cors_policy_->allowMethods();
}

const std::string& CorsFilter::allowHeaders() {
  if (route_cors_policy_->allowHeaders() != "") {
    return route_cors_policy_->allowHeaders();
  }
  return virtual_host_cors_policy_->allowHeaders();
}

const std::string& CorsFilter::exposeHeaders() {
  if (route_cors_policy_->exposeHeaders() != "") {
    return route_cors_policy_->exposeHeaders();
  }
  return virtual_host_cors_policy_->exposeHeaders();
}

const std::string& CorsFilter::maxAge() {
  if (route_cors_policy_->maxAge() != "") {
    return route_cors_policy_->maxAge();
  }
  return virtual_host_cors_policy_->maxAge();
}

bool CorsFilter::allowCredentials() {
  if (route_cors_policy_->allowCredentials() == true) {
    return route_cors_policy_->allowCredentials();
  }
  return virtual_host_cors_policy_->allowCredentials();
}

bool CorsFilter::enabled() {
  return route_cors_policy_->enabled() || virtual_host_cors_policy_->enabled();
}

} // namespace Http
} // namespace Envoy
