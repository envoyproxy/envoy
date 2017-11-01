#include "common/http/filter/cors_filter.h"

#include "envoy/http/codes.h"

#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Http {

CorsFilter::CorsFilter() : policies_({{nullptr, nullptr}}), is_cors_request_(false) {}

// This handles the CORS preflight request as described in #6.2
// https://www.w3.org/TR/cors/
FilterHeadersStatus CorsFilter::decodeHeaders(HeaderMap& headers, bool) {
  if (decoder_callbacks_->route() == nullptr ||
      decoder_callbacks_->route()->routeEntry() == nullptr) {
    return FilterHeadersStatus::Continue;
  }

  policies_ = {{
      decoder_callbacks_->route()->routeEntry()->corsPolicy(),
      decoder_callbacks_->route()->routeEntry()->virtualHost().corsPolicy(),
  }};

  if (!enabled()) {
    return FilterHeadersStatus::Continue;
  }

  origin_ = headers.Origin();
  if (origin_ == nullptr || origin_->value().empty()) {
    return FilterHeadersStatus::Continue;
  }

  if (!isOriginAllowed(origin_->value())) {
    return FilterHeadersStatus::Continue;
  }

  is_cors_request_ = true;

  const auto method = headers.Method();
  if (method == nullptr || method->value().c_str() != Http::Headers::get().MethodValues.Options) {
    return FilterHeadersStatus::Continue;
  }

  const auto requestMethod = headers.AccessControlRequestMethod();
  if (requestMethod == nullptr || requestMethod->value().empty()) {
    return FilterHeadersStatus::Continue;
  }

  HeaderMapPtr response_headers{
      new HeaderMapImpl{{Headers::get().Status, std::to_string(enumToInt(Http::Code::OK))}}};

  response_headers->insertAccessControlAllowOrigin().value(*origin_);

  if (allowCredentials()) {
    response_headers->insertAccessControlAllowCredentials().value(
        Http::Headers::get().CORSValues.True);
  }

  if (!allowMethods().empty()) {
    response_headers->insertAccessControlAllowMethods().value(allowMethods());
  }

  if (!allowHeaders().empty()) {
    response_headers->insertAccessControlAllowHeaders().value(allowHeaders());
  }

  if (!exposeHeaders().empty()) {
    response_headers->insertAccessControlExposeHeaders().value(exposeHeaders());
  }

  if (!maxAge().empty()) {
    response_headers->insertAccessControlMaxAge().value(maxAge());
  }

  decoder_callbacks_->encodeHeaders(std::move(response_headers), true);

  return FilterHeadersStatus::StopIteration;
}

// This handles simple CORS requests as described in #6.1
// https://www.w3.org/TR/cors/
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

bool CorsFilter::isOriginAllowed(const Http::HeaderString& origin) {
  if (allowOrigins() == nullptr) {
    return false;
  }
  for (const auto& o : *allowOrigins()) {
    if (o == "*" || origin == o.c_str()) {
      return true;
    }
  }
  return false;
}

const std::list<std::string>* CorsFilter::allowOrigins() {
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
    if (policy && policy->allowCredentials().valid()) {
      return policy->allowCredentials().value();
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

} // namespace Http
} // namespace Envoy
