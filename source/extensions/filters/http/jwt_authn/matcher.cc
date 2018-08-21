#include "extensions/filters/http/jwt_authn/header_matchers.h"

using ::envoy::api::v2::route::RouteMatch;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtProvider;
using ::envoy::config::filter::http::jwt_authn::v2alpha::RequirementRule;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

MatcherConstSharedPtr
Matcher::create(const RequirementRule& rule,
                const Protobuf::Map<ProtobufTypes::String, JwtProvider>& providers,
                const AuthFactory& factory) {
  switch (rule.match().path_specifier_case()) {
  case RouteMatch::PathSpecifierCase::kPrefix:
    return std::make_shared<PrefixMatcher>(rule, providers, factory);
  case RouteMatch::PathSpecifierCase::kPath:
    return std::make_shared<PathMatcher>(rule, providers, factory);
  case RouteMatch::PathSpecifierCase::kRegex:
    return std::make_shared<RegexMatcher>(rule, providers, factory);
  // path specifier is required.
  case RouteMatch::PathSpecifierCase::PATH_SPECIFIER_NOT_SET:
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
