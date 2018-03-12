#include "common/router/config_utility.h"

#include <regex>
#include <string>
#include <vector>

#include "common/common/assert.h"
#include "common/filesystem/filesystem_impl.h"

namespace Envoy {
namespace Router {

// HeaderMatcher will consist of one of the below two options:
// 1.value (string) and regex (bool)
//   An empty header value allows for matching to be only based on header presence.
//   Regex is an opt-in. Unless explicitly mentioned, the header values will be used for
//   exact string matching.
//   This is now deprecated.
// 2.header_match_specifier which can be any one of exact_match, regex_match or range_match.
//   Absence of these options implies empty header value match based on header presence.
//   a.exact_match: value will be used for exact string matching.
//   b.regex_match: Match will succeed if header value matches the value specified here.
//   c.range_match: Match will succeed if header value lies within the range specified
//     here, using half open interval semantics [start,end).
ConfigUtility::HeaderData::HeaderData(const envoy::api::v2::route::HeaderMatcher& config)
    : name_(config.name()) {
  switch (config.header_match_specifier_case()) {
  case envoy::api::v2::route::HeaderMatcher::kExactMatch:
    header_match_type_ = HeaderMatchType::Value;
    value_ = config.exact_match();
    break;
  case envoy::api::v2::route::HeaderMatcher::kRegexMatch:
    header_match_type_ = HeaderMatchType::Regex;
    regex_pattern_ = RegexUtil::parseRegex(config.regex_match());
    break;
  case envoy::api::v2::route::HeaderMatcher::kRangeMatch:
    header_match_type_ = HeaderMatchType::Range;
    range_.set_start(config.range_match().start());
    range_.set_end(config.range_match().end());
    break;
  case envoy::api::v2::route::HeaderMatcher::HEADER_MATCH_SPECIFIER_NOT_SET:
    FALLTHRU;
  default:
    if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, regex, false)) {
      header_match_type_ = HeaderMatchType::Regex;
      regex_pattern_ = RegexUtil::parseRegex(config.value());
    } else {
      header_match_type_ = HeaderMatchType::Value;
      value_ = config.value();
    }
    break;
  }
}

ConfigUtility::HeaderData::HeaderData(const Json::Object& config)
    : HeaderData([&config] {
        envoy::api::v2::route::HeaderMatcher header_matcher;
        Envoy::Config::RdsJson::translateHeaderMatcher(config, header_matcher);
        return header_matcher;
      }()) {}

bool ConfigUtility::QueryParameterMatcher::matches(
    const Http::Utility::QueryParams& request_query_params) const {
  auto query_param = request_query_params.find(name_);
  if (query_param == request_query_params.end()) {
    return false;
  } else if (is_regex_) {
    return std::regex_match(query_param->second, regex_pattern_);
  } else if (value_.length() == 0) {
    return true;
  } else {
    return (value_ == query_param->second);
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
    NOT_IMPLEMENTED;
  }
}

bool ConfigUtility::matchHeaders(const Http::HeaderMap& request_headers,
                                 const std::vector<HeaderData>& config_headers) {
  bool matches = true;

  if (!config_headers.empty()) {
    for (const HeaderData& cfg_header_data : config_headers) {
      const Http::HeaderEntry* header = request_headers.get(cfg_header_data.name_);

      if (header == nullptr) {
        matches = false;
        break;
      }

      switch (cfg_header_data.header_match_type_) {
      case HeaderMatchType::Value:
        matches &=
            (cfg_header_data.value_.empty() || header->value() == cfg_header_data.value_.c_str());
        break;

      case HeaderMatchType::Regex:
        matches &= std::regex_match(header->value().c_str(), cfg_header_data.regex_pattern_);
        break;

      case HeaderMatchType::Range: {
        int64_t header_value = 0;
        matches &= StringUtil::atol(header->value().c_str(), header_value, 10) &&
                   header_value >= cfg_header_data.range_.start() &&
                   header_value < cfg_header_data.range_.end();
        break;
      }

      default:
        NOT_REACHED;
      }

      if (!matches) {
        break;
      }
    }
  }

  return matches;
}

bool ConfigUtility::matchQueryParams(
    const Http::Utility::QueryParams& query_params,
    const std::vector<QueryParameterMatcher>& config_query_params) {
  for (const auto& config_query_param : config_query_params) {
    if (!config_query_param.matches(query_params)) {
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
    NOT_IMPLEMENTED;
  }
}

Optional<Http::Code>
ConfigUtility::parseDirectResponseCode(const envoy::api::v2::route::Route& route) {
  if (route.has_redirect()) {
    return parseRedirectResponseCode(route.redirect().response_code());
  } else if (route.has_direct_response()) {
    return static_cast<Http::Code>(route.direct_response().status());
  }
  return Optional<Http::Code>();
}

std::string ConfigUtility::parseDirectResponseBody(const envoy::api::v2::route::Route& route) {
  static const ssize_t MaxBodySize = 4096;
  if (!route.has_direct_response() || !route.direct_response().has_body()) {
    return EMPTY_STRING;
  }
  const auto& body = route.direct_response().body();
  const std::string filename = body.filename();
  if (!filename.empty()) {
    if (!Filesystem::fileExists(filename)) {
      throw EnvoyException(fmt::format("response body file {} does not exist", filename));
    }
    ssize_t size = Filesystem::fileSize(filename);
    if (size < 0) {
      throw EnvoyException(fmt::format("cannot determine size of response body file {}", filename));
    }
    if (size > MaxBodySize) {
      throw EnvoyException(fmt::format("response body file {} size is {} bytes; maximum is {}",
                                       filename, size, MaxBodySize));
    }
    return Filesystem::fileReadToEnd(filename);
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
    NOT_IMPLEMENTED;
  }
}

} // namespace Router
} // namespace Envoy
