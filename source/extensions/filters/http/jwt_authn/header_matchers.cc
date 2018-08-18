#include "extensions/filters/http/jwt_authn/header_matchers.h"

#include "envoy/config/filter/http/jwt_authn/v2alpha/config.pb.h"

using ::envoy::api::v2::route::RouteMatch;
using Envoy::Router::ConfigUtility;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

HeaderMatcher::HeaderMatcher(const RouteMatch& route)
    : case_sensitive_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(route, case_sensitive, true)) {
  for (const auto& header_map : route.headers()) {
    config_headers_.push_back(header_map);
  }

  for (const auto& query_parameter : route.query_parameters()) {
    config_query_parameters_.push_back(query_parameter);
  }
}

bool HeaderMatcher::matchRoute(const Http::HeaderMap& headers) const {
  bool matches = true;
  // TODO(potatop): matching on RouteMatch runtime is not implemented.

  matches &= Http::HeaderUtility::matchHeaders(headers, config_headers_);
  if (!config_query_parameters_.empty()) {
    Http::Utility::QueryParams query_parameters =
        Http::Utility::parseQueryString(headers.Path()->value().c_str());
    matches &= ConfigUtility::matchQueryParams(query_parameters, config_query_parameters_);
  }
  return matches;
}

bool PrefixMatcher::matches(const Http::HeaderMap& headers) const {
  if (HeaderMatcher::matchRoute(headers) &&
      StringUtil::startsWith(headers.Path()->value().c_str(), prefix_, case_sensitive_)) {
    ENVOY_LOG(debug, "Prefix requirement '{}' matched.", prefix_);
    return true;
  }
  return false;
}

bool PathMatcher::matches(const Http::HeaderMap& headers) const {
  if (HeaderMatcher::matchRoute(headers)) {
    const Http::HeaderString& path = headers.Path()->value();
    const char* query_string_start = Http::Utility::findQueryStringStart(path);
    size_t compare_length = path.size();
    if (query_string_start != nullptr) {
      compare_length = query_string_start - path.c_str();
    }

    if (compare_length != path_.size()) {
      return false;
    }
    bool match = case_sensitive_ ? 0 == strncmp(path.c_str(), path_.c_str(), compare_length)
                                 : 0 == strncasecmp(path.c_str(), path_.c_str(), compare_length);
    if (match) {
      ENVOY_LOG(debug, "Path requirement '{}' matched.", path_);
      return true;
    }
  }
  return false;
}

bool RegexMatcher::matches(const Http::HeaderMap& headers) const {
  if (HeaderMatcher::matchRoute(headers)) {
    const Http::HeaderString& path = headers.Path()->value();
    const char* query_string_start = Http::Utility::findQueryStringStart(path);
    if (std::regex_match(path.c_str(), query_string_start, regex_)) {
      ENVOY_LOG(debug, "Regex requirement '{}' matched.", regex_str_);
      return true;
    }
  }
  return false;
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
