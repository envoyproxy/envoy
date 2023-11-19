#include "source/common/router/config_utility.h"

#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/regex.h"
#include "source/common/config/datasource.h"

namespace Envoy {
namespace Router {
namespace {

absl::optional<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>
maybeCreateStringMatcher(const envoy::config::route::v3::QueryParameterMatcher& config) {
  switch (config.query_parameter_match_specifier_case()) {
  case envoy::config::route::v3::QueryParameterMatcher::QueryParameterMatchSpecifierCase::
      kStringMatch:
    return Matchers::StringMatcherImpl(config.string_match());
  case envoy::config::route::v3::QueryParameterMatcher::QueryParameterMatchSpecifierCase::
      kPresentMatch:
    return absl::nullopt;
  case envoy::config::route::v3::QueryParameterMatcher::QueryParameterMatchSpecifierCase::
      QUERY_PARAMETER_MATCH_SPECIFIER_NOT_SET:
    return absl::nullopt;
  }

  return absl::nullopt;
}

} // namespace

ConfigUtility::QueryParameterMatcher::QueryParameterMatcher(
    const envoy::config::route::v3::QueryParameterMatcher& config)
    : name_(config.name()), matcher_(maybeCreateStringMatcher(config)) {}

bool ConfigUtility::QueryParameterMatcher::matches(
    const Http::Utility::QueryParamsMulti& request_query_params) const {
  // This preserves the legacy behavior of ignoring all but the first value for a given key
  auto data = request_query_params.getFirstValue(name_);
  if (!data.has_value()) {
    return false;
  }

  if (!matcher_.has_value()) {
    // Present check
    return true;
  }

  return matcher_.value().match(data.value());
}

Upstream::ResourcePriority
ConfigUtility::parsePriority(const envoy::config::core::v3::RoutingPriority& priority) {
  switch (priority) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::core::v3::DEFAULT:
    return Upstream::ResourcePriority::Default;
  case envoy::config::core::v3::HIGH:
    return Upstream::ResourcePriority::High;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

bool ConfigUtility::matchQueryParams(
    const Http::Utility::QueryParamsMulti& query_params,
    const std::vector<QueryParameterMatcherPtr>& config_query_params) {
  for (const auto& config_query_param : config_query_params) {
    if (!config_query_param->matches(query_params)) {
      return false;
    }
  }

  return true;
}

Http::Code ConfigUtility::parseRedirectResponseCode(
    const envoy::config::route::v3::RedirectAction::RedirectResponseCode& code) {
  switch (code) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::route::v3::RedirectAction::MOVED_PERMANENTLY:
    return Http::Code::MovedPermanently;
  case envoy::config::route::v3::RedirectAction::FOUND:
    return Http::Code::Found;
  case envoy::config::route::v3::RedirectAction::SEE_OTHER:
    return Http::Code::SeeOther;
  case envoy::config::route::v3::RedirectAction::TEMPORARY_REDIRECT:
    return Http::Code::TemporaryRedirect;
  case envoy::config::route::v3::RedirectAction::PERMANENT_REDIRECT:
    return Http::Code::PermanentRedirect;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

absl::optional<Http::Code>
ConfigUtility::parseDirectResponseCode(const envoy::config::route::v3::Route& route) {
  if (route.has_redirect()) {
    return parseRedirectResponseCode(route.redirect().response_code());
  } else if (route.has_direct_response()) {
    return static_cast<Http::Code>(route.direct_response().status());
  }
  return {};
}

std::string ConfigUtility::parseDirectResponseBody(const envoy::config::route::v3::Route& route,
                                                   Api::Api& api, uint32_t max_body_size_bytes) {
  if (!route.has_direct_response() || !route.direct_response().has_body()) {
    return EMPTY_STRING;
  }
  const auto& body = route.direct_response().body();

  const std::string string_body =
      Envoy::Config::DataSource::read(body, true, api, max_body_size_bytes);
  if (string_body.length() > max_body_size_bytes) {
    throwEnvoyExceptionOrPanic(fmt::format("response body size is {} bytes; maximum is {}",
                                           string_body.length(), max_body_size_bytes));
  }
  return string_body;
}

Http::Code ConfigUtility::parseClusterNotFoundResponseCode(
    const envoy::config::route::v3::RouteAction::ClusterNotFoundResponseCode& code) {
  switch (code) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::route::v3::RouteAction::SERVICE_UNAVAILABLE:
    return Http::Code::ServiceUnavailable;
  case envoy::config::route::v3::RouteAction::NOT_FOUND:
    return Http::Code::NotFound;
  case envoy::config::route::v3::RouteAction::INTERNAL_SERVER_ERROR:
    return Http::Code::InternalServerError;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

} // namespace Router
} // namespace Envoy
