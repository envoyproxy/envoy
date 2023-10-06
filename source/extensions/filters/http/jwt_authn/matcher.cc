#include "source/extensions/filters/http/jwt_authn/matcher.h"

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/common/common/regex.h"
#include "source/common/http/path_utility.h"
#include "source/common/router/config_impl.h"

#include "absl/strings/match.h"

using envoy::config::route::v3::RouteMatch;
using envoy::extensions::filters::http::jwt_authn::v3::RequirementRule;
using Envoy::Router::ConfigUtility;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

/**
 * Perform a match against any HTTP header or pseudo-header.
 */
class BaseMatcherImpl : public Matcher, public Logger::Loggable<Logger::Id::jwt> {
public:
  BaseMatcherImpl(const RequirementRule& rule)
      : case_sensitive_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(rule.match(), case_sensitive, true)),
        config_headers_(Http::HeaderUtility::buildHeaderDataVector(rule.match().headers())) {
    for (const auto& query_parameter : rule.match().query_parameters()) {
      config_query_parameters_.push_back(
          std::make_unique<Router::ConfigUtility::QueryParameterMatcher>(query_parameter));
    }
  }

  // Check match for HeaderMatcher and QueryParameterMatcher
  bool matchRoute(const Http::RequestHeaderMap& headers) const {
    bool matches = true;
    // TODO(potatop): matching on RouteMatch runtime is not implemented.

    matches &= Http::HeaderUtility::matchHeaders(headers, config_headers_);
    if (!config_query_parameters_.empty()) {
      Http::Utility::QueryParamsMulti query_parameters =
          Http::Utility::QueryParamsMulti::parseQueryString(headers.getPathValue());
      matches &= ConfigUtility::matchQueryParams(query_parameters, config_query_parameters_);
    }
    return matches;
  }

protected:
  const bool case_sensitive_;

private:
  std::vector<Http::HeaderUtility::HeaderDataPtr> config_headers_;
  std::vector<Router::ConfigUtility::QueryParameterMatcherPtr> config_query_parameters_;
};

/**
 * Perform a match against any path with prefix rule.
 */
class PrefixMatcherImpl : public BaseMatcherImpl {
public:
  PrefixMatcherImpl(const RequirementRule& rule)
      : BaseMatcherImpl(rule), prefix_(rule.match().prefix()),
        path_matcher_(Matchers::PathMatcher::createPrefix(prefix_, !case_sensitive_)) {}

  bool matches(const Http::RequestHeaderMap& headers) const override {
    if (BaseMatcherImpl::matchRoute(headers) && path_matcher_->match(headers.getPathValue())) {
      ENVOY_LOG(debug, "Prefix requirement '{}' matched.", prefix_);
      return true;
    }
    return false;
  }

private:
  // prefix string
  const std::string prefix_;
  const Matchers::PathMatcherConstSharedPtr path_matcher_;
};

/**
 * Perform a match against any path with a specific path rule.
 */
class PathMatcherImpl : public BaseMatcherImpl {
public:
  PathMatcherImpl(const RequirementRule& rule)
      : BaseMatcherImpl(rule), path_(rule.match().path()),
        path_matcher_(Matchers::PathMatcher::createExact(path_, !case_sensitive_)) {}

  bool matches(const Http::RequestHeaderMap& headers) const override {
    if (BaseMatcherImpl::matchRoute(headers) && path_matcher_->match(headers.getPathValue())) {
      ENVOY_LOG(debug, "Path requirement '{}' matched.", path_);
      return true;
    }
    return false;
  }

private:
  // path string.
  const std::string path_;
  const Matchers::PathMatcherConstSharedPtr path_matcher_;
};

/**
 * Perform a match against any path with a regex rule.
 * TODO(mattklein123): This code needs dedup with RegexRouteEntryImpl.
 */
class RegexMatcherImpl : public BaseMatcherImpl {
public:
  RegexMatcherImpl(const RequirementRule& rule)
      : BaseMatcherImpl(rule), regex_str_(rule.match().safe_regex().regex()),
        path_matcher_(Matchers::PathMatcher::createSafeRegex(rule.match().safe_regex())) {
    ASSERT(rule.match().path_specifier_case() ==
           envoy::config::route::v3::RouteMatch::PathSpecifierCase::kSafeRegex);
  }

  bool matches(const Http::RequestHeaderMap& headers) const override {
    if (BaseMatcherImpl::matchRoute(headers)) {
      if (headers.Path() == nullptr) {
        return false;
      }
      const Http::HeaderString& path = headers.Path()->value();
      const absl::string_view query_string = Http::Utility::findQueryStringStart(path);
      absl::string_view path_view = path.getStringView();
      path_view.remove_suffix(query_string.length());
      if (path_matcher_->match(path_view)) {
        ENVOY_LOG(debug, "Regex requirement '{}' matched.", regex_str_);
        return true;
      }
    }
    return false;
  }

private:
  // raw regex string, for logging.
  const std::string regex_str_;
  const Matchers::PathMatcherConstSharedPtr path_matcher_;
};

/**
 * Perform a match against an HTTP CONNECT request.
 */
class ConnectMatcherImpl : public BaseMatcherImpl {
public:
  ConnectMatcherImpl(const RequirementRule& rule) : BaseMatcherImpl(rule) {}

  bool matches(const Http::RequestHeaderMap& headers) const override {
    if (Http::HeaderUtility::isConnect(headers) && BaseMatcherImpl::matchRoute(headers)) {
      ENVOY_LOG(debug, "CONNECT requirement matched.");
      return true;
    }

    return false;
  }
};

class PathSeparatedPrefixMatcherImpl : public BaseMatcherImpl {
public:
  PathSeparatedPrefixMatcherImpl(const RequirementRule& rule)
      : BaseMatcherImpl(rule), prefix_(rule.match().path_separated_prefix()),
        path_matcher_(Matchers::PathMatcher::createPrefix(prefix_, !case_sensitive_)) {}

  bool matches(const Http::RequestHeaderMap& headers) const override {
    if (!BaseMatcherImpl::matchRoute(headers)) {
      return false;
    }
    absl::string_view path = Http::PathUtil::removeQueryAndFragment(headers.getPathValue());
    if (path.size() >= prefix_.size() && path_matcher_->match(path) &&
        (path.size() == prefix_.size() || path[prefix_.size()] == '/')) {
      ENVOY_LOG(debug, "Path-separated prefix requirement '{}' matched.", prefix_);
      return true;
    }
    return false;
  }

private:
  // prefix string
  const std::string prefix_;
  const Matchers::PathMatcherConstSharedPtr path_matcher_;
};
} // namespace

MatcherConstPtr Matcher::create(const RequirementRule& rule) {
  switch (rule.match().path_specifier_case()) {
  case RouteMatch::PathSpecifierCase::kPrefix:
    return std::make_unique<PrefixMatcherImpl>(rule);
  case RouteMatch::PathSpecifierCase::kPath:
    return std::make_unique<PathMatcherImpl>(rule);
  case RouteMatch::PathSpecifierCase::kSafeRegex:
    return std::make_unique<RegexMatcherImpl>(rule);
  case RouteMatch::PathSpecifierCase::kConnectMatcher:
    return std::make_unique<ConnectMatcherImpl>(rule);
  case RouteMatch::PathSpecifierCase::kPathSeparatedPrefix:
    return std::make_unique<PathSeparatedPrefixMatcherImpl>(rule);
  case RouteMatch::PathSpecifierCase::kPathMatchPolicy:
    // TODO(silverstar194): Implement matcher for template based match
    throw EnvoyException("RouteMatch: path_match_policy is not supported");
    break;
  case RouteMatch::PathSpecifierCase::PATH_SPECIFIER_NOT_SET:
    break; // Fall through to PANIC.
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
