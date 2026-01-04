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

#include "absl/strings/numbers.h"

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

ConfigUtility::CookieMatcher::CookieMatcher(const envoy::config::route::v3::CookieMatcher& config,
                                            Server::Configuration::CommonFactoryContext& context)
    : name_(config.name()), invert_match_(config.invert_match()),
      treat_missing_as_empty_(config.treat_missing_cookie_as_empty()) {
  switch (config.cookie_match_specifier_case()) {
  case envoy::config::route::v3::CookieMatcher::CookieMatchSpecifierCase::kStringMatch:
    string_match_.emplace(config.string_match(), context);
    break;
  case envoy::config::route::v3::CookieMatcher::CookieMatchSpecifierCase::kRangeMatch:
    range_match_.emplace(RangeMatch{config.range_match().start(), config.range_match().end()});
    break;
  case envoy::config::route::v3::CookieMatcher::CookieMatchSpecifierCase::kPresentMatch:
    present_match_ = config.present_match();
    break;
  case envoy::config::route::v3::CookieMatcher::CookieMatchSpecifierCase::
      COOKIE_MATCH_SPECIFIER_NOT_SET:
    present_match_ = true;
    break;
  }

  if (!present_match_.has_value() && !string_match_.has_value() && !range_match_.has_value()) {
    present_match_ = true;
  }
}

bool ConfigUtility::CookieMatcher::matches(
    const absl::optional<absl::string_view>& cookie_value) const {
  if (string_match_.has_value()) {
    const bool has_value = cookie_value.has_value() || treat_missing_as_empty_;
    bool matched = false;
    if (has_value) {
      const absl::string_view value = cookie_value.value_or(EMPTY_STRING);
      matched = string_match_->match(value);
    }
    return matched != invert_match_;
  }

  if (range_match_.has_value()) {
    const bool has_value = cookie_value.has_value() || treat_missing_as_empty_;
    bool matched = false;
    if (has_value) {
      const absl::string_view value = cookie_value.value_or(EMPTY_STRING);
      int64_t parsed_value = 0;
      matched = absl::SimpleAtoi(value, &parsed_value) && parsed_value >= range_match_->start &&
                parsed_value < range_match_->end;
    }
    return matched != invert_match_;
  }

  const bool has_cookie = cookie_value.has_value();
  const bool expected_presence = present_match_.value_or(true);
  const bool matched = has_cookie == expected_presence;
  return matched != invert_match_;
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

bool ConfigUtility::matchCookies(const absl::flat_hash_map<std::string, std::string>& cookies,
                                 const std::vector<CookieMatcherPtr>& matchers) {
  if (matchers.empty()) {
    return true;
  }

  for (const auto& matcher : matchers) {
    absl::optional<absl::string_view> cookie_value;
    const auto it = cookies.find(matcher->name());
    if (it != cookies.end()) {
      cookie_value = it->second;
    }
    if (!matcher->matches(cookie_value)) {
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
