#include "common/router/config_utility.h"

#include <string>
#include <vector>

#include "common/common/assert.h"
#include "common/common/regex.h"

namespace Envoy {
namespace Router {
namespace {

absl::optional<Matchers::StringMatcherImpl>
maybeCreateStringMatcher(const envoy::api::v2::route::QueryParameterMatcher& config) {
  switch (config.query_parameter_match_specifier_case()) {
  case envoy::api::v2::route::QueryParameterMatcher::kStringMatch: {
    return Matchers::StringMatcherImpl(config.string_match());
  }
  case envoy::api::v2::route::QueryParameterMatcher::kPresentMatch: {
    return absl::nullopt;
  }
  case envoy::api::v2::route::QueryParameterMatcher::QUERY_PARAMETER_MATCH_SPECIFIER_NOT_SET: {
    if (config.value().empty()) {
      // Present match.
      return absl::nullopt;
    }

    envoy::type::matcher::StringMatcher matcher_config;
    if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, regex, false)) {
      matcher_config.set_regex(config.value());
    } else {
      matcher_config.set_exact(config.value());
    }
    return Matchers::StringMatcherImpl(matcher_config);
  }
  }

  NOT_REACHED_GCOVR_EXCL_LINE; // Needed for gcc
}

} // namespace

ConfigUtility::QueryParameterMatcher::QueryParameterMatcher(
    const envoy::api::v2::route::QueryParameterMatcher& config)
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
ConfigUtility::parsePriority(const envoy::api::v2::core::RoutingPriority& priority) {
  switch (priority) {
  case envoy::api::v2::core::RoutingPriority::DEFAULT:
    return Upstream::ResourcePriority::Default;
  case envoy::api::v2::core::RoutingPriority::HIGH:
    return Upstream::ResourcePriority::High;
  default:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
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
    const envoy::api::v2::route::RedirectAction::RedirectResponseCode& code) {
  switch (code) {
  case envoy::api::v2::route::RedirectAction::MOVED_PERMANENTLY:
    return Http::Code::MovedPermanently;
  case envoy::api::v2::route::RedirectAction::FOUND:
    return Http::Code::Found;
  case envoy::api::v2::route::RedirectAction::SEE_OTHER:
    return Http::Code::SeeOther;
  case envoy::api::v2::route::RedirectAction::TEMPORARY_REDIRECT:
    return Http::Code::TemporaryRedirect;
  case envoy::api::v2::route::RedirectAction::PERMANENT_REDIRECT:
    return Http::Code::PermanentRedirect;
  default:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
}

absl::optional<Http::Code>
ConfigUtility::parseDirectResponseCode(const envoy::api::v2::route::Route& route) {
  if (route.has_redirect()) {
    return parseRedirectResponseCode(route.redirect().response_code());
  } else if (route.has_direct_response()) {
    return static_cast<Http::Code>(route.direct_response().status());
  }
  return {};
}

std::string ConfigUtility::parseDirectResponseBody(const envoy::api::v2::route::Route& route,
                                                   Api::Api& api) {
  static const ssize_t MaxBodySize = 4096;
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
      throw EnvoyException(fmt::format("cannot determine size of response body file {}", filename));
    }
    if (size > MaxBodySize) {
      throw EnvoyException(fmt::format("response body file {} size is {} bytes; maximum is {}",
                                       filename, size, MaxBodySize));
    }
    return api.fileSystem().fileReadToEnd(filename);
  }
  const std::string inline_body(body.inline_bytes().empty() ? body.inline_string()
                                                            : body.inline_bytes());
  if (inline_body.length() > MaxBodySize) {
    throw EnvoyException(fmt::format("response body size is {} bytes; maximum is {}",
                                     inline_body.length(), MaxBodySize));
  }
  return inline_body;
}

Http::Code ConfigUtility::parseClusterNotFoundResponseCode(
    const envoy::api::v2::route::RouteAction::ClusterNotFoundResponseCode& code) {
  switch (code) {
  case envoy::api::v2::route::RouteAction::SERVICE_UNAVAILABLE:
    return Http::Code::ServiceUnavailable;
  case envoy::api::v2::route::RouteAction::NOT_FOUND:
    return Http::Code::NotFound;
  default:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
}

} // namespace Router
} // namespace Envoy
