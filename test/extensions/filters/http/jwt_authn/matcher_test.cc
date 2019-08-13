#include "common/protobuf/utility.h"

#include "extensions/filters/http/jwt_authn/matcher.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/test_common/utility.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::RequirementRule;
using Envoy::Http::TestHeaderMapImpl;

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
  auto headers = TestHeaderMapImpl{{":path", "/match/this"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/MATCH"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/matching"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/matc"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/no"}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchRegex) {
  const char config[] = R"(match:
  regex: "/[^c][au]t")";
  RequirementRule rule;
  TestUtility::loadFromYaml(config, rule);
  MatcherConstPtr matcher = Matcher::create(rule);
  auto headers = TestHeaderMapImpl{{":path", "/but"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/mat?ok=bye"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/maut"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/cut"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/mut/"}};
  EXPECT_FALSE(matcher->matches(headers));
}

TEST_F(MatcherTest, TestMatchPath) {
  const char config[] = R"(match:
  path: "/match"
  case_sensitive: false)";
  RequirementRule rule;
  TestUtility::loadFromYaml(config, rule);
  MatcherConstPtr matcher = Matcher::create(rule);
  auto headers = TestHeaderMapImpl{{":path", "/match"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/MATCH"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/match?ok=bye"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/matc"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/match/"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/matching"}};
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
  auto headers = TestHeaderMapImpl{{":path", "/boo?foo=bar"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/boo?ok=bye"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/foo?bar=bar"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/boo?foo"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/boo?bar=foo"}};
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
  auto headers = TestHeaderMapImpl{{":path", "/"}, {"a", ""}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/"}, {"a", "some"}, {"b", ""}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/"}, {"aa", ""}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/"}, {"", ""}};
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
  auto headers = TestHeaderMapImpl{{":path", "/boo?foo=bar"}};
  EXPECT_TRUE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/boo?ok=bye"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/foo?bar=bar"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/boo?foo"}};
  EXPECT_FALSE(matcher->matches(headers));
  headers = TestHeaderMapImpl{{":path", "/boo?bar=foo"}};
  EXPECT_FALSE(matcher->matches(headers));
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
