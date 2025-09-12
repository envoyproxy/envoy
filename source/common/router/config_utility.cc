#include "source/common/router/config_utility.h"

#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/regex.h"
#include "source/common/config/datasource.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Router {
namespace {

absl::optional<Matchers::StringMatcherImpl>
maybeCreateStringMatcher(const envoy::config::route::v3::QueryParameterMatcher& config,
                         Server::Configuration::CommonFactoryContext& context) {
  switch (config.query_parameter_match_specifier_case()) {
  case envoy::config::route::v3::QueryParameterMatcher::QueryParameterMatchSpecifierCase::
      kStringMatch:
    return Matchers::StringMatcherImpl(config.string_match(), context);
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
    const envoy::config::route::v3::QueryParameterMatcher& config,
    Server::Configuration::CommonFactoryContext& context)
    : name_(config.name()),
      present_match_(config.has_present_match() ? absl::make_optional(config.present_match())
                                                : absl::nullopt),
      matcher_(maybeCreateStringMatcher(config, context)) {}

bool ConfigUtility::QueryParameterMatcher::matches(
    const Http::Utility::QueryParamsMulti& request_query_params) const {
  // This preserves the legacy behavior of ignoring all but the first value for a given key
  auto data = request_query_params.getFirstValue(name_);

  // If we're doing a present_match, return whether the parameter exists and matches the expected
  // presence
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.enable_new_query_param_present_match_behavior") &&
      present_match_.has_value()) {
    return data.has_value() == present_match_.value();
  }

  // If the parameter doesn't exist, no match
  if (!data.has_value()) {
    return false;
  }

  // If there's no matcher, treat it as a present check
  if (!matcher_.has_value()) {
    return true;
  }

  // Match the value against the string matcher
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

void mergeTransforms(Http::HeaderTransforms& dest, const Http::HeaderTransforms& src) {
  dest.headers_to_append_or_add.insert(dest.headers_to_append_or_add.end(),
                                       src.headers_to_append_or_add.begin(),
                                       src.headers_to_append_or_add.end());
  dest.headers_to_overwrite_or_add.insert(dest.headers_to_overwrite_or_add.end(),
                                          src.headers_to_overwrite_or_add.begin(),
                                          src.headers_to_overwrite_or_add.end());
  dest.headers_to_add_if_absent.insert(dest.headers_to_add_if_absent.end(),
                                       src.headers_to_add_if_absent.begin(),
                                       src.headers_to_add_if_absent.end());
  dest.headers_to_remove.insert(dest.headers_to_remove.end(), src.headers_to_remove.begin(),
                                src.headers_to_remove.end());
}

} // namespace Router
} // namespace Envoy
