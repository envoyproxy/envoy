#include "extensions/filters/http/cors/cors_filter.h"

#include "envoy/http/codes.h"

#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {

CorsFilter::CorsFilter() : policies_({{nullptr, nullptr}}), is_cors_request_(false) {}

// This handles the CORS preflight request as described in #6.2
// https://www.w3.org/TR/cors/
Http::FilterHeadersStatus CorsFilter::decodeHeaders(Http::HeaderMap& headers, bool) {
  if (decoder_callbacks_->route() == nullptr ||
      decoder_callbacks_->route()->routeEntry() == nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }

  policies_ = {{
      decoder_callbacks_->route()->routeEntry()->corsPolicy(),
      decoder_callbacks_->route()->routeEntry()->virtualHost().corsPolicy(),
  }};

  if (!enabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  origin_ = headers.Origin();
  if (origin_ == nullptr || origin_->value().empty()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (!isOriginAllowed(origin_->value())) {
    return Http::FilterHeadersStatus::Continue;
  }

  is_cors_request_ = true;

  const auto method = headers.Method();
  if (method == nullptr || method->value().c_str() != Http::Headers::get().MethodValues.Options) {
    return Http::FilterHeadersStatus::Continue;
  }

  const auto requestMethod = headers.AccessControlRequestMethod();
  if (requestMethod == nullptr || requestMethod->value().empty()) {
    return Http::FilterHeadersStatus::Continue;
  }

  Http::HeaderMapPtr response_headers{new Http::HeaderMapImpl{
      {Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::OK))}}};

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

  return Http::FilterHeadersStatus::StopIteration;
}

// This handles simple CORS requests as described in #6.1
// https://www.w3.org/TR/cors/
Http::FilterHeadersStatus CorsFilter::encodeHeaders(Http::HeaderMap& headers, bool) {
  if (!is_cors_request_) {
    return Http::FilterHeadersStatus::Continue;
  }

  headers.insertAccessControlAllowOrigin().value(*origin_);
  if (allowCredentials()) {
    headers.insertAccessControlAllowCredentials().value(Http::Headers::get().CORSValues.True);
  }

  return Http::FilterHeadersStatus::Continue;
}

void CorsFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

bool CorsFilter::isOriginAllowed(const Http::HeaderString& origin) {
  return isOriginAllowedString(origin) || isOriginAllowedRegex(origin);
}

bool CorsFilter::isOriginAllowedString(const Http::HeaderString& origin) {
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

bool CorsFilter::isOriginAllowedRegex(const Http::HeaderString& origin) {
  if (allowOriginRegexes() == nullptr) {
    return false;
  }
  for (const auto& regex : *allowOriginRegexes()) {
    if (std::regex_match(origin.c_str(), regex)) {
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

const std::list<std::regex>* CorsFilter::allowOriginRegexes() {
  for (const auto policy : policies_) {
    if (policy && !policy->allowOriginRegexes().empty()) {
      return &policy->allowOriginRegexes();
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
