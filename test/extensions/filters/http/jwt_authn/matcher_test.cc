#include "common/protobuf/utility.h"

#include "extensions/filters/http/jwt_authn/matcher.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/test_common/utility.h"

using ::envoy::api::v2::route::RouteMatch;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtProvider;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtRequirement;
using ::envoy::config::filter::http::jwt_authn::v2alpha::RequirementRule;
using Envoy::Http::TestHeaderMapImpl;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;

using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

class MatcherTest : public ::testing::Test {
public:
  NiceMock<MockAuthFactory> mock_factory_;
  MockExtractor extractor_;
};

TEST_F(MatcherTest, TestMatchPrefix) {
  const char config[] = R"(match:
  prefix: "/match")";
  RequirementRule rule;
  MessageUtil::loadFromYaml(config, rule);
  MatcherConstSharedPtr matcher = Matcher::create(
      rule, Protobuf::Map<ProtobufTypes::String, JwtProvider>(), mock_factory_, extractor_);
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
  EXPECT_FALSE(matcher->verifier() == nullptr);
}

TEST_F(MatcherTest, TestMatchRegex) {
  const char config[] = R"(match:
  regex: "/[^c][au]t")";
  RequirementRule rule;
  MessageUtil::loadFromYaml(config, rule);
  MatcherConstSharedPtr matcher = Matcher::create(
      rule, Protobuf::Map<ProtobufTypes::String, JwtProvider>(), mock_factory_, extractor_);
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
  EXPECT_FALSE(matcher->verifier() == nullptr);
}

TEST_F(MatcherTest, TestMatchPath) {
  const char config[] = R"(match:
  path: "/match"
  case_sensitive: false)";
  RequirementRule rule;
  MessageUtil::loadFromYaml(config, rule);
  MatcherConstSharedPtr matcher = Matcher::create(
      rule, Protobuf::Map<ProtobufTypes::String, JwtProvider>(), mock_factory_, extractor_);
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
  EXPECT_FALSE(matcher->verifier() == nullptr);
}

TEST_F(MatcherTest, TestMatchQuery) {
  const char config[] = R"(match:
  prefix: "/"
  query_parameters:
  - name: foo
    value: bar)";
  RequirementRule rule;
  MessageUtil::loadFromYaml(config, rule);
  MatcherConstSharedPtr matcher = Matcher::create(
      rule, Protobuf::Map<ProtobufTypes::String, JwtProvider>(), mock_factory_, extractor_);
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
  EXPECT_FALSE(matcher->verifier() == nullptr);
}

TEST_F(MatcherTest, TestMatchHeader) {
  const char config[] = R"(match:
  prefix: "/"
  headers:
  - name: a)";
  RequirementRule rule;
  MessageUtil::loadFromYaml(config, rule);
  MatcherConstSharedPtr matcher = Matcher::create(
      rule, Protobuf::Map<ProtobufTypes::String, JwtProvider>(), mock_factory_, extractor_);
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
  EXPECT_FALSE(matcher->verifier() == nullptr);
}

TEST_F(MatcherTest, TestMatchPathAndHeader) {
  const char config[] = R"(match:
  path: "/boo"
  query_parameters:
  - name: foo
    value: bar)";
  RequirementRule rule;
  MessageUtil::loadFromYaml(config, rule);
  MatcherConstSharedPtr matcher = Matcher::create(
      rule, Protobuf::Map<ProtobufTypes::String, JwtProvider>(), mock_factory_, extractor_);
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
  EXPECT_FALSE(matcher->verifier() == nullptr);
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
