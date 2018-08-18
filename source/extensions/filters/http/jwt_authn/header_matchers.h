#pragma once

#include "envoy/api/v2/route/route.pb.h"

#include "common/common/logger.h"
#include "common/common/utility.h"
#include "common/http/header_utility.h"
#include "common/router/config_impl.h"

#include "extensions/filters/http/jwt_authn/matcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

/**
 * Perform a match against any HTTP header or pseudo-header.
 */
class HeaderMatcher : public Matcher, public Logger::Loggable<Logger::Id::filter> {
public:
  HeaderMatcher(const ::envoy::api::v2::route::RouteMatch& route);
  bool matchRoute(const Http::HeaderMap& headers) const;

protected:
  const bool case_sensitive_;
  // const MatcherConstSharedPtr requires_matcher_;

private:
  std::vector<Http::HeaderUtility::HeaderData> config_headers_;
  std::vector<Router::ConfigUtility::QueryParameterMatcher> config_query_parameters_;
};

/**
 * Perform a match against any path with prefix rule.
 */
class PrefixMatcher : public HeaderMatcher {
public:
  PrefixMatcher(const ::envoy::api::v2::route::RouteMatch& route)
      : HeaderMatcher(route), prefix_(route.prefix()) {}

  bool matches(const Http::HeaderMap& headers) const override;

private:
  // prefix string
  const std::string prefix_;
};

/**
 * Perform a match against any path with a specific path rule.
 */
class PathMatcher : public HeaderMatcher {
public:
  PathMatcher(const ::envoy::api::v2::route::RouteMatch& route)
      : HeaderMatcher(route), path_(route.path()) {}

  bool matches(const Http::HeaderMap& headers) const override;

private:
  // path string.
  const std::string path_;
};

/**
 * Perform a match against any path with a regex rule.
 */
class RegexMatcher : public HeaderMatcher {
public:
  RegexMatcher(const ::envoy::api::v2::route::RouteMatch& route)
      : HeaderMatcher(route), regex_(RegexUtil::parseRegex(route.regex())),
        regex_str_(route.regex()) {}

  bool matches(const Http::HeaderMap& headers) const override;

private:
  // regex object
  const std::regex regex_;
  // raw regex string, for logging.
  const std::string regex_str_;
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
