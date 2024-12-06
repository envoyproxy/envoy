#include <memory>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/registry/registry.h"

#include "source/common/matcher/matcher.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/matching/data_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/common/matcher/domain_matcher.h"

#include "test/common/matcher/test_utility.h"
#include "test/mocks/matcher/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "xds/type/matcher/v3/matcher.pb.h"
#include "xds/type/matcher/v3/matcher.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {
namespace {

using ::Envoy::Matcher::ActionFactory;
using ::Envoy::Matcher::CustomMatcherFactory;
using ::Envoy::Matcher::DataInputGetResult;
using ::Envoy::Matcher::MatchTreeFactory;
using ::Envoy::Matcher::MockMatchTreeValidationVisitor;
using ::Envoy::Matcher::StringAction;
using ::Envoy::Matcher::StringActionFactory;
using ::Envoy::Matcher::TestData;
using ::Envoy::Matcher::TestDataInputBoolFactory;
using ::Envoy::Matcher::TestDataInputStringFactory;

class DomainMatcherTest : public ::testing::Test {
public:
  DomainMatcherTest()
      : inject_action_(action_factory_), inject_matcher_(domain_matcher_factory_),
        factory_(context_, factory_context_, validation_visitor_) {
    EXPECT_CALL(validation_visitor_, performDataInputValidation(_, _)).Times(testing::AnyNumber());
  }

  void loadConfig(const std::string& config) {
    MessageUtil::loadFromYaml(config, matcher_, ProtobufMessage::getStrictValidationVisitor());
    TestUtility::validate(matcher_);
  }
  void validateMatch(const std::string& output) {
    auto match_tree = factory_.create(matcher_);
    const auto result = match_tree()->match(TestData());
    EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
    EXPECT_TRUE(result.on_match_.has_value());
    EXPECT_NE(result.on_match_->action_cb_, nullptr);
    auto action = result.on_match_->action_cb_();
    const auto value = action->getTyped<StringAction>();
    EXPECT_EQ(value.string_, output);
  }
  void validateNoMatch() {
    auto match_tree = factory_.create(matcher_);
    const auto result = match_tree()->match(TestData());
    EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
    EXPECT_FALSE(result.on_match_.has_value());
  }
  void validateUnableToMatch() {
    auto match_tree = factory_.create(matcher_);
    const auto result = match_tree()->match(TestData());
    EXPECT_EQ(result.match_state_, MatchState::UnableToMatch);
  }

  StringActionFactory action_factory_;
  Registry::InjectFactory<ActionFactory<absl::string_view>> inject_action_;
  DomainTrieMatcherFactoryBase<TestData> domain_matcher_factory_;
  Registry::InjectFactory<CustomMatcherFactory<TestData>> inject_matcher_;
  MockMatchTreeValidationVisitor<TestData> validation_visitor_;

  absl::string_view context_ = "";
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  MatchTreeFactory<TestData, absl::string_view> factory_;
  xds::type::matcher::v3::Matcher matcher_;
};

TEST_F(DomainMatcherTest, TestDomainMatcher) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: domain_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers:
      - domains:
        - "api.example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: exact_match
      - domains:
        - "*.example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: wildcard_match
      - domains:
        - "*.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: global_wildcard
  )EOF";
  loadConfig(yaml);

  {
    auto input = TestDataInputStringFactory("api.example.com");
    validateMatch("exact_match");
  }
  {
    auto input = TestDataInputStringFactory("foo.example.com");
    validateMatch("wildcard_match");
  }
  {
    auto input = TestDataInputStringFactory("something.else.com");
    validateMatch("global_wildcard");
  }
}

TEST_F(DomainMatcherTest, TestDomainMatcherOrdering) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: domain_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers:
      - domains:
        - "foo.bar.example.com"
        - "*.example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: specific_wildcard
      - domains:
        - "*.bar.example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: double_wildcard
  )EOF";
  loadConfig(yaml);

  {
    auto input = TestDataInputStringFactory("foo.bar.example.com");
    validateMatch("specific_wildcard");
  }
  {
    auto input = TestDataInputStringFactory("other.bar.example.com");
    validateMatch("double_wildcard");
  }
}

TEST_F(DomainMatcherTest, TestEmptyDomainMatcher) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: domain_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers: []
  )EOF";
  loadConfig(yaml);
  auto input = TestDataInputStringFactory("example.com");
  validateNoMatch();
}

TEST_F(DomainMatcherTest, TestNestedWildcards) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: domain_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers:
      - domains:
        - "*.api.example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: deep_wildcard
  )EOF";
  loadConfig(yaml);

  {
    auto input = TestDataInputStringFactory("test.api.example.com");
    validateMatch("deep_wildcard");
  }
  {
    auto input = TestDataInputStringFactory("api.example.com");
    validateNoMatch();
  }
}

TEST_F(DomainMatcherTest, TestOnNoMatch) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: domain_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers:
      - domains:
        - "example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: exact
on_no_match:
  action:
    name: test_action
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: no_match
  )EOF";
  loadConfig(yaml);

  {
    auto input = TestDataInputStringFactory("other.com");
    validateMatch("no_match");
  }
  {
    auto input = TestDataInputStringFactory("");
    validateMatch("no_match");
  }
}

TEST_F(DomainMatcherTest, TestDataAvailability) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: domain_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers:
      - domains:
        - "example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: match
  )EOF";
  loadConfig(yaml);

  {
    auto input = TestDataInputStringFactory(
        {DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()});
    validateUnableToMatch();
  }
  {
    auto input = TestDataInputStringFactory(
        {DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()});
    validateNoMatch();
  }
}

TEST_F(DomainMatcherTest, TestInvalidInput) {
  const std::string multiple_wildcard = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.FloatValue
  custom_match:
    name: domain_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers:
      - domains:
        - "*.example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: invalid
  )EOF";
  loadConfig(multiple_wildcard);
  auto input = ::Envoy::Matcher::TestDataInputFloatFactory(3.14);
  std::string error_message = absl::StrCat("Unsupported data input type: float, currently only "
                                           "string type is supported in domain matcher");
  auto match_tree = factory_.create(matcher_);
  EXPECT_THROW_WITH_MESSAGE(match_tree(), EnvoyException, error_message);
}

TEST_F(DomainMatcherTest, TestMultipleWildcardsInvalidation) {
  const std::string multiple_wildcard = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: domain_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers:
      - domains:
        - "*.*.example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: invalid
  )EOF";
  loadConfig(multiple_wildcard);
  auto input = TestDataInputStringFactory("example.com");
  EXPECT_THROW_WITH_MESSAGE(factory_.create(matcher_), EnvoyException,
                            "Invalid wildcard domain format: *.*.example.com");
}

TEST_F(DomainMatcherTest, TestInvalidDomainValidation) {
  {
    const std::string invalid_wildcard = R"EOF(
    matcher_tree:
      input:
        name: input
        typed_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
      custom_match:
        name: domain_matcher
        typed_config:
          "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
          domain_matchers:
          - domains:
            - "*something.com"
            on_match:
              action:
                name: test_action
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: invalid
    )EOF";
    loadConfig(invalid_wildcard);
    auto input = TestDataInputStringFactory("something.com");
    EXPECT_THROW_WITH_MESSAGE(factory_.create(matcher_), EnvoyException,
                              "Invalid wildcard domain format: *something.com");
  }

  {
    const std::string duplicate_domain = R"EOF(
    matcher_tree:
      input:
        name: input
        typed_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
      custom_match:
        name: domain_matcher
        typed_config:
          "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
          domain_matchers:
          - domains:
            - "example.com"
            - "example.com"
            on_match:
              action:
                name: test_action
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: duplicate
    )EOF";
    loadConfig(duplicate_domain);
    auto input = TestDataInputStringFactory("example.com");
    EXPECT_THROW_WITH_MESSAGE(factory_.create(matcher_), EnvoyException,
                              "Duplicate domain in ServerNameMatcher: example.com");
  }
}

TEST(DomainMatcherIntegrationTest, HttpMatchingData) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: request-headers
    typed_config:
      "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
      header_name: :authority
  custom_match:
    name: domain_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers:
      - domains:
        - "*.example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: matching
  )EOF";
  xds::type::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  StringActionFactory action_factory;
  Registry::InjectFactory<ActionFactory<absl::string_view>> inject_action(action_factory);
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  MockMatchTreeValidationVisitor<Http::HttpMatchingData> validation_visitor;
  EXPECT_CALL(validation_visitor, performDataInputValidation(_, _)).Times(testing::AnyNumber());

  absl::string_view context = "";
  MatchTreeFactory<Http::HttpMatchingData, absl::string_view> matcher_factory(
      context, factory_context, validation_visitor);
  auto match_tree = matcher_factory.create(matcher);

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  Http::TestRequestHeaderMapImpl headers;
  headers.addCopy(Http::Headers::get().Host, "api.example.com");

  Http::Matching::HttpMatchingDataImpl data(stream_info);
  data.onRequestHeaders(headers);

  const auto result = match_tree()->match(data);
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
  auto action = result.on_match_->action_cb_();
  const auto value = action->getTyped<StringAction>();
  EXPECT_EQ(value.string_, "matching");
}

TEST_F(DomainMatcherTest, TestGlobalWildcardDomain) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: domain_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers:
      - domains:
        - "*"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: global_wildcard_match
  )EOF";
  loadConfig(yaml);

  TestDataInputStringFactory factory("example.com");
  auto match_tree = factory_.create(matcher_);
  const auto result = match_tree()->match(TestData());
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
  auto action = result.on_match_->action_cb_();
  const auto value = action->getTyped<StringAction>();
  EXPECT_EQ(value.string_, "global_wildcard_match");
}

TEST_F(DomainMatcherTest, TestWildcardPrecedence) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: domain_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers:
      - domains:
        - "exact.example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: exact_match
      - domains:
        - "*.example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: specific_wildcard
      - domains:
        - "*"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: global_wildcard
  )EOF";
  loadConfig(yaml);

  // Test exact match
  {
    TestDataInputStringFactory factory("exact.example.com");
    auto match_tree = factory_.create(matcher_);
    const auto result = match_tree()->match(TestData());
    EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
    EXPECT_TRUE(result.on_match_.has_value());
    auto action = result.on_match_->action_cb_();
    const auto value = action->getTyped<StringAction>();
    EXPECT_EQ(value.string_, "exact_match");
  }

  // Test specific wildcard match
  {
    TestDataInputStringFactory factory("test.example.com");
    auto match_tree = factory_.create(matcher_);
    const auto result = match_tree()->match(TestData());
    EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
    EXPECT_TRUE(result.on_match_.has_value());
    auto action = result.on_match_->action_cb_();
    const auto value = action->getTyped<StringAction>();
    EXPECT_EQ(value.string_, "specific_wildcard");
  }

  // Test global wildcard match
  {
    TestDataInputStringFactory factory("something.else.com");
    auto match_tree = factory_.create(matcher_);
    const auto result = match_tree()->match(TestData());
    EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
    EXPECT_TRUE(result.on_match_.has_value());
    auto action = result.on_match_->action_cb_();
    const auto value = action->getTyped<StringAction>();
    EXPECT_EQ(value.string_, "global_wildcard");
  }
}

TEST_F(DomainMatcherTest, TestEmptyPartsAfterWildcard) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: domain_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.ServerNameMatcher
      domain_matchers:
      - domains:
        - "*."
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: should_not_match
  )EOF";
  loadConfig(yaml);

  TestDataInputStringFactory factory("test.com");
  auto match_tree = factory_.create(matcher_);
  const auto result = match_tree()->match(TestData());
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_FALSE(result.on_match_.has_value());
}

} // namespace
} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
