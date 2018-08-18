#include "envoy/config/filter/http/jwt_authn/v2alpha/config.pb.h"

#include "extensions/filters/http/jwt_authn/header_matchers.h"
#include "extensions/filters/http/jwt_authn/requirement_matchers.h"

using ::envoy::api::v2::route::RouteMatch;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirement;
using ::envoy::config::filter::http::jwt_authn::v2alpha::RequirementRule;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {} // namespace

MatcherConstSharedPtr Matcher::create(const RouteMatch& route) {
  switch (route.path_specifier_case()) {
  case RouteMatch::PathSpecifierCase::kPrefix:
    return std::make_shared<PrefixMatcher>(route);
  case RouteMatch::PathSpecifierCase::kPath:
    return std::make_shared<PathMatcher>(route);
  case RouteMatch::PathSpecifierCase::kRegex:
    return std::make_shared<RegexMatcher>(route);
  case RouteMatch::PathSpecifierCase::PATH_SPECIFIER_NOT_SET:
    return std::make_shared<PrefixMatcher>(route);
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

AsyncMatcherSharedPtr AsyncMatcher::create(const JwtRequirement& requirement,
                                           FilterConfigSharedPtr config) {
  switch (requirement.requires_type_case()) {
  case JwtRequirement::RequiresTypeCase::kProviderName:
    return std::make_shared<ProviderNameMatcher>(requirement.provider_name(), config,
                                                 Authenticator::create(config));
  case JwtRequirement::RequiresTypeCase::kProviderAndAudiences: {
    std::vector<std::string> audiences;
    for (const auto& it : requirement.provider_and_audiences().audiences()) {
      audiences.push_back(it);
    }
    return std::make_shared<ProviderNameMatcher>(
        requirement.provider_and_audiences().provider_name(), config,
        Authenticator::create(config, audiences));
  }
  case JwtRequirement::RequiresTypeCase::kRequiresAny:
    return std::make_shared<AnyMatcher>(requirement.requires_any(), config);
  case JwtRequirement::RequiresTypeCase::kRequiresAll:
    return std::make_shared<AllMatcher>(requirement.requires_all(), config);
  case JwtRequirement::RequiresTypeCase::kAllowMissingOrFailed:
    return std::make_shared<AllowFailedMatcher>(Authenticator::create(config));
  case JwtRequirement::RequiresTypeCase::REQUIRES_TYPE_NOT_SET:
    return std::make_shared<AllowAllMatcher>();
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

AsyncMatcherSharedPtr AsyncMatcher::create(const RequirementRule& rule,
                                           FilterConfigSharedPtr config) {
  return std::make_shared<RuleMatcher>(rule, config);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
