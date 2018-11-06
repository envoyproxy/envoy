#include "extensions/filters/http/jwt_authn/matcher.h"

#include "common/router/config_impl.h"

using ::envoy::api::v2::route::RouteMatch;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtProvider;
using ::envoy::config::filter::http::jwt_authn::v2alpha::RequirementRule;
using Envoy::Router::ConfigUtility;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

/**
 * Perform a match against any HTTP header or pseudo-header.
 */
class BaseMatcherImpl : public Matcher, public Logger::Loggable<Logger::Id::filter> {
public:
  BaseMatcherImpl(const RequirementRule& rule,
                  const Protobuf::Map<ProtobufTypes::String, JwtProvider>& providers,
                  const AuthFactory& factory, const Extractor& extractor)
      : case_sensitive_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(rule.match(), case_sensitive, true)) {

    for (const auto& header_map : rule.match().headers()) {
      config_headers_.push_back(header_map);
    }

    for (const auto& query_parameter : rule.match().query_parameters()) {
      config_query_parameters_.push_back(query_parameter);
    }

    verifier_ = Verifier::create(rule.requires(), providers, factory, extractor);
  }

  // Check match for HeaderMatcher and QueryParameterMatcher
  bool matchRoute(const Http::HeaderMap& headers) const {
    bool matches = true;
    // TODO(potatop): matching on RouteMatch runtime is not implemented.

    matches &= Http::HeaderUtility::matchHeaders(headers, config_headers_);
    if (!config_query_parameters_.empty()) {
      Http::Utility::QueryParams query_parameters =
          Http::Utility::parseQueryString(headers.Path()->value().getStringView());
      matches &= ConfigUtility::matchQueryParams(query_parameters, config_query_parameters_);
    }
    return matches;
  }

  const VerifierPtr& verifier() const override { return verifier_; }

protected:
  const bool case_sensitive_;

private:
  std::vector<Http::HeaderUtility::HeaderData> config_headers_;
  std::vector<Router::ConfigUtility::QueryParameterMatcher> config_query_parameters_;
  VerifierPtr verifier_;
};

/**
 * Perform a match against any path with prefix rule.
 */
class PrefixMatcherImpl : public BaseMatcherImpl {
public:
  PrefixMatcherImpl(const RequirementRule& rule,
                    const Protobuf::Map<ProtobufTypes::String, JwtProvider>& providers,
                    const AuthFactory& factory, const Extractor& extractor)
      : BaseMatcherImpl(rule, providers, factory, extractor), prefix_(rule.match().prefix()) {}

  bool matches(const Http::HeaderMap& headers) const override {
    if (BaseMatcherImpl::matchRoute(headers) &&
        StringUtil::startsWith(headers.Path()->value().c_str(), prefix_, case_sensitive_)) {
      ENVOY_LOG(debug, "Prefix requirement '{}' matched.", prefix_);
      return true;
    }
    return false;
  }

private:
  // prefix string
  const std::string prefix_;
};

/**
 * Perform a match against any path with a specific path rule.
 */
class PathMatcherImpl : public BaseMatcherImpl {
public:
  PathMatcherImpl(const RequirementRule& rule,
                  const Protobuf::Map<ProtobufTypes::String, JwtProvider>& providers,
                  const AuthFactory& factory, const Extractor& extractor)
      : BaseMatcherImpl(rule, providers, factory, extractor), path_(rule.match().path()) {}

  bool matches(const Http::HeaderMap& headers) const override {
    if (BaseMatcherImpl::matchRoute(headers)) {
      const Http::HeaderString& path = headers.Path()->value();
      size_t compare_length = Http::Utility::findQueryStringStart(path) - path.c_str();

      auto real_path = path.getStringView().substr(0, compare_length);
      bool match = case_sensitive_ ? real_path == path_ : StringUtil::caseCompare(real_path, path_);
      if (match) {
        ENVOY_LOG(debug, "Path requirement '{}' matched.", path_);
        return true;
      }
    }
    return false;
  }

private:
  // path string.
  const std::string path_;
};

/**
 * Perform a match against any path with a regex rule.
 */
class RegexMatcherImpl : public BaseMatcherImpl {
public:
  RegexMatcherImpl(const RequirementRule& rule,
                   const Protobuf::Map<ProtobufTypes::String, JwtProvider>& providers,
                   const AuthFactory& factory, const Extractor& extractor)
      : BaseMatcherImpl(rule, providers, factory, extractor),
        regex_(RegexUtil::parseRegex(rule.match().regex())), regex_str_(rule.match().regex()) {}

  bool matches(const Http::HeaderMap& headers) const override {
    if (BaseMatcherImpl::matchRoute(headers)) {
      const Http::HeaderString& path = headers.Path()->value();
      const char* query_string_start = Http::Utility::findQueryStringStart(path);
      if (std::regex_match(path.c_str(), query_string_start, regex_)) {
        ENVOY_LOG(debug, "Regex requirement '{}' matched.", regex_str_);
        return true;
      }
    }
    return false;
  }

private:
  // regex object
  const std::regex regex_;
  // raw regex string, for logging.
  const std::string regex_str_;
};

} // namespace

MatcherConstSharedPtr
Matcher::create(const RequirementRule& rule,
                const Protobuf::Map<ProtobufTypes::String, JwtProvider>& providers,
                const AuthFactory& factory, const Extractor& extractor) {
  switch (rule.match().path_specifier_case()) {
  case RouteMatch::PathSpecifierCase::kPrefix:
    return std::make_shared<PrefixMatcherImpl>(rule, providers, factory, extractor);
  case RouteMatch::PathSpecifierCase::kPath:
    return std::make_shared<PathMatcherImpl>(rule, providers, factory, extractor);
  case RouteMatch::PathSpecifierCase::kRegex:
    return std::make_shared<RegexMatcherImpl>(rule, providers, factory, extractor);
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
