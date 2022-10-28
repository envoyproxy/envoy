#include "source/common/router/config_utility.h"

#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/regex.h"

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
    const Http::Utility::QueryParams& request_query_params) const {
  auto query_param = request_query_params.find(name_);
  if (query_param == request_query_params.end()) {
    return false;
  } else if (!matcher_.has_value()) {
    // Present match.
    return true;
  } else {
    return matcher_.value().match(query_param->second);
  }
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
    const Http::Utility::QueryParams& query_params,
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
  const std::string& filename = body.filename();
  if (!filename.empty()) {
    if (!api.fileSystem().fileExists(filename)) {
      throw EnvoyException(fmt::format("response body file {} does not exist", filename));
    }
    const ssize_t size = api.fileSystem().fileSize(filename);
    if (size < 0) {
      throw EnvoyException(absl::StrCat("cannot determine size of response body file ", filename));
    }
    if (static_cast<uint64_t>(size) > max_body_size_bytes) {
      throw EnvoyException(fmt::format("response body file {} size is {} bytes; maximum is {}",
                                       filename, size, max_body_size_bytes));
    }
    return api.fileSystem().fileReadToEnd(filename);
  }
  const std::string inline_body(body.inline_bytes().empty() ? body.inline_string()
                                                            : body.inline_bytes());
  if (inline_body.length() > max_body_size_bytes) {
    throw EnvoyException(fmt::format("response body size is {} bytes; maximum is {}",
                                     inline_body.length(), max_body_size_bytes));
  }
  return inline_body;
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
