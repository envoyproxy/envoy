#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/http/jwt_authn/matcher.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
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

TEST_F(MatcherTest, TestMatchRegex) {
  const char config[] = R"(match:
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
    value: bar)";
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
    value: bar)";
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

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
