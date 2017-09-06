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

  if (allowOrigin().find(origin_->value().c_str()) == std::string::npos && allowOrigin() != "*") {
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
  routeCorsPolicy_ = decoder_callbacks_->route()->routeEntry()->corsPolicy();
  virtualHostCorsPolicy_ = decoder_callbacks_->route()->routeEntry()->virtualHost().corsPolicy();
}

const std::string& CorsFilter::allowOrigin() {
  if (routeCorsPolicy_->allowOrigin() != "") {
    return routeCorsPolicy_->allowOrigin();
  }
  return virtualHostCorsPolicy_->allowOrigin();
}

const std::string& CorsFilter::allowMethods() {
  if (routeCorsPolicy_->allowMethods() != "") {
    return routeCorsPolicy_->allowMethods();
  }
  return virtualHostCorsPolicy_->allowMethods();
}

const std::string& CorsFilter::allowHeaders() {
  if (routeCorsPolicy_->allowHeaders() != "") {
    return routeCorsPolicy_->allowHeaders();
  }
  return virtualHostCorsPolicy_->allowHeaders();
}

const std::string& CorsFilter::exposeHeaders() {
  if (routeCorsPolicy_->exposeHeaders() != "") {
    return routeCorsPolicy_->exposeHeaders();
  }
  return virtualHostCorsPolicy_->exposeHeaders();
}

const std::string& CorsFilter::maxAge() {
  if (routeCorsPolicy_->maxAge() != "") {
    return routeCorsPolicy_->maxAge();
  }
  return virtualHostCorsPolicy_->maxAge();
}

bool CorsFilter::allowCredentials() {
  if (routeCorsPolicy_->allowCredentials() == true) {
    return routeCorsPolicy_->allowCredentials();
  }
  return virtualHostCorsPolicy_->allowCredentials();
}

bool CorsFilter::enabled() {
  return routeCorsPolicy_->enabled() || virtualHostCorsPolicy_->enabled();
}

} // namespace Http
} // namespace Envoy
