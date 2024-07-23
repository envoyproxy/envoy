#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/jwt_authn/matcher.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

using envoy::extensions::filters::http::jwt_authn::v3::RequirementRule;
using Envoy::Http::TestRequestHeaderMapImpl;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

class MatcherTest : public testing::Test {
public:
  MatcherConstPtr createMatcher(const char* config) {
    RequirementRule rule;
    TestUtility::loadFromYaml(config, rule);
    return Matcher::create(rule, context_);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(MatcherTest, TestMatchPrefix) {
  const char config[] = R"(match:
  prefix: "/match")";
  MatcherConstPtr matcher = createMatcher(config);
  auto headers = TestRequestHeaderMapImpl{{":path", "/match/this"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/MATCH"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/matching"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/matc"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/no"}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchSafeRegex) {
  const char config[] = R"(
match:
  safe_regex:
    google_re2: {}
    regex: "/[^c][au]t")";

  MatcherConstPtr matcher = createMatcher(config);
  auto headers = TestRequestHeaderMapImpl{{":path", "/but"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/mat?ok=bye"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/maut"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/cut"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/mut/"}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchPath) {
  const char config[] = R"(match:
  path: "/match"
  case_sensitive: false)";
  MatcherConstPtr matcher = createMatcher(config);
  auto headers = TestRequestHeaderMapImpl{{":path", "/match"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/MATCH"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/match?ok=bye"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/matc"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/match/"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/matching"}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchQuery) {
  const char config[] = R"(match:
  prefix: "/"
  query_parameters:
  - name: foo
    string_match:
      exact: bar)";
  MatcherConstPtr matcher = createMatcher(config);
  auto headers = TestRequestHeaderMapImpl{{":path", "/boo?foo=bar"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/boo?ok=bye"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/foo?bar=bar"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/boo?foo"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/boo?bar=foo"}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchHeader) {
  const char config[] = R"(match:
  prefix: "/"
  headers:
  - name: a)";
  MatcherConstPtr matcher = createMatcher(config);
  auto headers = TestRequestHeaderMapImpl{{":path", "/"}, {"a", ""}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/"}, {"a", "some"}, {"b", ""}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/"}, {"aa", ""}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/"}, {"", ""}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchPathAndHeader) {
  const char config[] = R"(match:
  path: "/boo"
  query_parameters:
  - name: foo
    string_match:
      exact: bar)";
  MatcherConstPtr matcher = createMatcher(config);
  auto headers = TestRequestHeaderMapImpl{{":path", "/boo?foo=bar"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/boo?ok=bye"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/foo?bar=bar"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/boo?foo"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/boo?bar=foo"}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchConnect) {
  const char config[] = R"(match:
  connect_matcher: {})";
  MatcherConstPtr matcher = createMatcher(config);
  auto headers = TestRequestHeaderMapImpl{{":method", "CONNECT"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":method", "GET"}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchConnectQuery) {
  const char config[] = R"(match:
  connect_matcher: {}
  query_parameters:
  - name: foo
    string_match:
      exact: "bar")";
  MatcherConstPtr matcher = createMatcher(config);
  auto headers = TestRequestHeaderMapImpl{{":method", "CONNECT"}, {":path", "/boo?foo=bar"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":method", "GET"}, {":path", "/boo?foo=bar"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":method", "CONNECT"}, {":path", "/boo?ok=bye"}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchPathSeparatedPrefix) {
  const char config[] = R"(match:
  path_separated_prefix: "/rest/api")";
  MatcherConstPtr matcher = createMatcher(config);

  // Exact matches
  auto headers = TestRequestHeaderMapImpl{{":path", "/rest/api"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/rest/api?param=true"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/rest/api#fragment"}};
  EXPECT_TRUE(matcher->matches(headers));

  // Prefix matches
  headers = TestRequestHeaderMapImpl{{":path", "/rest/api/"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/rest/api/thing?param=true"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/rest/api/thing#fragment"}};
  EXPECT_TRUE(matcher->matches(headers));

  // Non-matching prefixes
  headers = TestRequestHeaderMapImpl{{":path", "/rest/apithing"}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchPathSeparatedPrefixCaseSensitivity) {

  const char configCaseSensitive[] = R"(match:
  path_separated_prefix: "/rest/API")";
  MatcherConstPtr matcherSensitive = createMatcher(configCaseSensitive);

  const char configCaseSensitiveExplicit[] = R"(match:
  path_separated_prefix: "/rest/API"
  case_sensitive: true)";
  MatcherConstPtr matcherSensitiveExplicit = createMatcher(configCaseSensitiveExplicit);

  const char configCaseInsensitive[] = R"(match:
  path_separated_prefix: "/rest/api"
  case_sensitive: false)";
  MatcherConstPtr matcherInsensitive = createMatcher(configCaseInsensitive);

  auto headers = TestRequestHeaderMapImpl{{":path", "/rest/API"}};
  EXPECT_TRUE(matcherSensitive->matches(headers));
  EXPECT_TRUE(matcherSensitiveExplicit->matches(headers));
  EXPECT_TRUE(matcherInsensitive->matches(headers));

  headers = TestRequestHeaderMapImpl{{":path", "/rest/API/"}};
  EXPECT_TRUE(matcherSensitive->matches(headers));
  EXPECT_TRUE(matcherSensitiveExplicit->matches(headers));
  EXPECT_TRUE(matcherInsensitive->matches(headers));

  headers = TestRequestHeaderMapImpl{{":path", "/rest/API?param=true"}};
  EXPECT_TRUE(matcherSensitive->matches(headers));
  EXPECT_TRUE(matcherSensitiveExplicit->matches(headers));
  EXPECT_TRUE(matcherInsensitive->matches(headers));

  headers = TestRequestHeaderMapImpl{{":path", "/rest/API/thing?param=true"}};
  EXPECT_TRUE(matcherSensitive->matches(headers));
  EXPECT_TRUE(matcherSensitiveExplicit->matches(headers));
  EXPECT_TRUE(matcherInsensitive->matches(headers));

  headers = TestRequestHeaderMapImpl{{":path", "/REST/API"}};
  EXPECT_FALSE(matcherSensitive->matches(headers));
  EXPECT_FALSE(matcherSensitiveExplicit->matches(headers));
  EXPECT_TRUE(matcherInsensitive->matches(headers));
}

TEST_F(MatcherTest, TestMatchPathSeparatedPrefixBaseCondition) {
  const char config[] = R"(match:
  path_separated_prefix: "/rest/api"
  query_parameters:
  - name: param
    string_match:
      exact: test
  headers:
  - name: cookies)";
  MatcherConstPtr matcher = createMatcher(config);
  auto headers = TestRequestHeaderMapImpl{{":path", "/rest/api?param=test"}, {"cookies", ""}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/rest/api?param=test"}, {"pizza", ""}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/rest/api"}, {"cookies", ""}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchPathMatchPolicy) {
  const char config[] = R"(match:
  path_match_policy:
    name: envoy.path.match.uri_template.uri_template_matcher
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.path.match.uri_template.v3.UriTemplateMatchConfig
      path_template: "/bar/*/foo"
  )";
  MatcherConstPtr matcher = createMatcher(config);
  auto headers = TestRequestHeaderMapImpl{{":path", "/bar/test/foo"}};
  EXPECT_TRUE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchPathMatchPolicyError) {
  const char config[] = R"(match:
  path_match_policy:
    name: envoy.path.match.uri_template.uri_template_matcher
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.path.match.uri_template.v3.UriTemplateMatchConfig
      wrong_key: "/bar/*/foo"
  )";
  EXPECT_THROW_WITH_REGEX(createMatcher(config), EnvoyException, "INVALID_ARGUMENT")
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
