#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/jwt_authn/matcher.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/test_common/test_runtime.h"
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
};

TEST_F(MatcherTest, TestMatchPrefix) {
  const char config[] = R"(match:
  prefix: "/match")";
  RequirementRule rule;
  TestUtility::loadFromYaml(config, rule);
  MatcherConstPtr matcher = Matcher::create(rule);
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

  RequirementRule rule;
  TestUtility::loadFromYaml(config, rule);
  MatcherConstPtr matcher = Matcher::create(rule);
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
  RequirementRule rule;
  TestUtility::loadFromYaml(config, rule);
  MatcherConstPtr matcher = Matcher::create(rule);
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
  RequirementRule rule;
  TestUtility::loadFromYaml(config, rule);
  MatcherConstPtr matcher = Matcher::create(rule);
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
  RequirementRule rule;
  TestUtility::loadFromYaml(config, rule);
  MatcherConstPtr matcher = Matcher::create(rule);
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
  RequirementRule rule;
  TestUtility::loadFromYaml(config, rule);
  MatcherConstPtr matcher = Matcher::create(rule);
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
  RequirementRule rule;
  TestUtility::loadFromYaml(config, rule);
  MatcherConstPtr matcher = Matcher::create(rule);
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
  RequirementRule rule;
  TestUtility::loadFromYaml(config, rule);
  MatcherConstPtr matcher = Matcher::create(rule);
  auto headers = TestRequestHeaderMapImpl{{":method", "CONNECT"}, {":path", "/boo?foo=bar"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":method", "GET"}, {":path", "/boo?foo=bar"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":method", "CONNECT"}, {":path", "/boo?ok=bye"}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchPathSeparatedPrefix) {
  const char config[] = R"(match:
  path_separated_prefix: "/api")";
  RequirementRule rule;
  TestUtility::loadFromYaml(config, rule);
  MatcherConstPtr matcher = Matcher::create(rule);
  auto headers = TestRequestHeaderMapImpl{{":path", "/api"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/ApI"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/api?foo=bar"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/api#foobar"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/api/"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/api/new"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/apinew"}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchPathSeparatedPrefixCaseSensitive) {
  const char config[] = R"(match:
  path_separated_prefix: "/api"
  case_sensitive: false)";
  RequirementRule rule;
  TestUtility::loadFromYaml(config, rule);
  MatcherConstPtr matcher = Matcher::create(rule);
  auto headers = TestRequestHeaderMapImpl{{":path", "/Api"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/Api/"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/Api/new"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/Apinew"}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchPathSeparatedPrefixQueryHeader) {
  const char config[] = R"(match:
  path_separated_prefix: "/api"
  query_parameters:
  - name: foo
    string_match:
      exact: bar
  headers:
  - name: cookies)";
  RequirementRule rule;
  TestUtility::loadFromYaml(config, rule);
  MatcherConstPtr matcher = Matcher::create(rule);
  auto headers = TestRequestHeaderMapImpl{{":path", "/api?foo=bar"}, {"cookies", ""}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/api?foo=bar"}, {"pizza", ""}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestRequestHeaderMapImpl{{":path", "/api"}, {"cookies", ""}};
  EXPECT_FALSE(matcher->matches(headers));
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
