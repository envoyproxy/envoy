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
  initialize();

  origin_ = headers.Origin();
  if (origin_ == nullptr || origin_->value() == "") {
    return FilterHeadersStatus::Continue;
  }

  is_cors_request_ = true;

  const Http::HeaderEntry* method = headers.Method();
  if (method != nullptr && method->value().c_str() != Http::Headers::get().MethodValues.Options) {
    return FilterHeadersStatus::Continue;
  }

  if (!cors_enabled_) {
    return FilterHeadersStatus::Continue;
  }

  if (allow_origin_ == "") {
    return FilterHeadersStatus::Continue;
  }

  HeaderMapPtr response_headers{
      new HeaderMapImpl{{Headers::get().Status, std::to_string(enumToInt(Http::Code::OK))}}};

  if (allow_credentials_) {
    response_headers->insertAccessControlAllowCredentials().value(
        Http::Headers::get().CORSValues.True);
    response_headers->insertAccessControlAllowOrigin().value(*origin_);
  } else {
    response_headers->insertAccessControlAllowOrigin().value(allow_origin_);
  }

  auto requestMethod = headers.AccessControlRequestMethod();
  if (requestMethod == nullptr || requestMethod->value() == "") {
    return FilterHeadersStatus::Continue;
  }

  if (allow_methods_ != "") {
    response_headers->insertAccessControlAllowMethods().value(allow_methods_);
  }

  if (allow_headers_ != "") {
    response_headers->insertAccessControlAllowHeaders().value(allow_headers_);
  }

  if (expose_headers_ != "") {
    response_headers->insertAccessControlExposeHeaders().value(expose_headers_);
  }

  if (max_age_ != "") {
    response_headers->insertAccessControlMaxAge().value(max_age_);
  }

  response_headers->insertStatus().value(enumToInt(Http::Code::OK));
  decoder_callbacks_->encodeHeaders(std::move(response_headers), true);

  return FilterHeadersStatus::StopIteration;
}

FilterHeadersStatus CorsFilter::encodeHeaders(HeaderMap& headers, bool) {
  if (!cors_enabled_ || !is_cors_request_) {
    return FilterHeadersStatus::Continue;
  }

  if (allow_credentials_) {
    headers.insertAccessControlAllowCredentials().value(Http::Headers::get().CORSValues.True);
    headers.insertAccessControlAllowOrigin().value(*origin_);
  } else {
    headers.insertAccessControlAllowOrigin().value(allow_origin_);
  }

  return FilterHeadersStatus::Continue;
}

void CorsFilter::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
};

void CorsFilter::initialize() {
  auto routeEntry = decoder_callbacks_->route()->routeEntry();
  const Envoy::Router::CorsPolicy& routeCorsPolicy = routeEntry->corsPolicy();
  const Envoy::Router::CorsPolicy& virtualHostCorsPolicy = routeEntry->virtualHost().corsPolicy();

  if (routeCorsPolicy.enabled() && virtualHostCorsPolicy.enabled()) {
    cors_enabled_ = true;
  }

  allow_origin_ = routeCorsPolicy.allowOrigin();
  if (allow_origin_ == "") {
    allow_origin_ = virtualHostCorsPolicy.allowOrigin();
  }

  allow_credentials_ = routeCorsPolicy.allowCredentials();
  if (allow_credentials_ == true) {
    allow_credentials_ = virtualHostCorsPolicy.allowCredentials();
  }

  allow_methods_ = routeCorsPolicy.allowMethods();
  if (allow_methods_ == "") {
    allow_methods_ = virtualHostCorsPolicy.allowMethods();
  }

  allow_headers_ = routeCorsPolicy.allowHeaders();
  if (allow_headers_ == "") {
    allow_headers_ = virtualHostCorsPolicy.allowHeaders();
  }

  expose_headers_ = routeCorsPolicy.exposeHeaders();
  if (expose_headers_ == "") {
    expose_headers_ = virtualHostCorsPolicy.exposeHeaders();
  }

  max_age_ = routeCorsPolicy.maxAge();
  if (max_age_ == "") {
    max_age_ = virtualHostCorsPolicy.maxAge();
  }
}

void CorsFilter::onDestroy() {}

} // namespace Http
} // namespace Envoy
