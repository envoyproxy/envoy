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
using ::Envoy::Matcher::HasInsufficientData;
using ::Envoy::Matcher::HasNoMatch;
using ::Envoy::Matcher::HasStringAction;
using ::Envoy::Matcher::IsStringAction;
using ::Envoy::Matcher::MatchResult;
using ::Envoy::Matcher::MatchTreeFactory;
using ::Envoy::Matcher::MockMatchTreeValidationVisitor;
using ::Envoy::Matcher::SkippedMatchCb;
using ::Envoy::Matcher::StringActionFactory;
using ::Envoy::Matcher::TestData;
using ::Envoy::Matcher::TestDataInputBoolFactory;
using ::Envoy::Matcher::TestDataInputStringFactory;
using ::testing::ElementsAre;

class DomainMatcherTest : public ::testing::Test {
public:
  DomainMatcherTest()
      : inject_action_(action_factory_), inject_matcher_(domain_matcher_factory_),
        input_string_factory_("input"), inject_input_string_(input_string_factory_),
        input_float_factory_(3.14f), inject_input_float_(input_float_factory_),
        factory_(context_, factory_context_, validation_visitor_) {
    EXPECT_CALL(validation_visitor_, performDataInputValidation(_, _)).Times(testing::AnyNumber());
  }

  void loadConfig(const std::string& config) {
    MessageUtil::loadFromYaml(config, matcher_, ProtobufMessage::getStrictValidationVisitor());
    TestUtility::validate(matcher_);
  }

  MatchResult doMatch() {
    auto match_tree = factory_.create(matcher_);
    return match_tree()->match(TestData(), skipped_match_cb_);
  }

  void validateMatch(const std::string& output) {
    const auto result = doMatch();
    EXPECT_THAT(result, HasStringAction(output));
  }

  void validateNoMatch() {
    const auto result = doMatch();
    EXPECT_THAT(result, HasNoMatch());
  }

  void validateUnableToMatch() {
    const auto result = doMatch();
    EXPECT_THAT(result, HasInsufficientData());
  }

  StringActionFactory action_factory_;
  Registry::InjectFactory<ActionFactory<absl::string_view>> inject_action_;
  DomainTrieMatcherFactoryBase<TestData> domain_matcher_factory_;
  Registry::InjectFactory<CustomMatcherFactory<TestData>> inject_matcher_;
  TestDataInputStringFactory input_string_factory_;
  Registry::InjectFactory<::Envoy::Matcher::DataInputFactory<TestData>> inject_input_string_;
  ::Envoy::Matcher::TestDataInputFloatFactory input_float_factory_;
  Registry::InjectFactory<::Envoy::Matcher::DataInputFactory<TestData>> inject_input_float_;
  MockMatchTreeValidationVisitor<TestData> validation_visitor_;

  absl::string_view context_ = "";
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  MatchTreeFactory<TestData, absl::string_view> factory_;
  xds::type::matcher::v3::Matcher matcher_;
  SkippedMatchCb skipped_match_cb_ = nullptr;
};

TEST_F(DomainMatcherTest, BasicDomainMatching) {
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

  // Test exact match (highest priority).
  {
    auto input = TestDataInputStringFactory("api.example.com");
    validateMatch("exact_match");
  }

  // Test wildcard match (middle priority).
  {
    auto input = TestDataInputStringFactory("foo.example.com");
    validateMatch("wildcard_match");
  }

  // Test broader wildcard match (lower priority).
  {
    auto input = TestDataInputStringFactory("something.else.com");
    validateMatch("global_wildcard");
  }
}

TEST_F(DomainMatcherTest, WildcardPrecedenceOrder) {
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
        - "*.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: short_wildcard
      - domains:
        - "*.example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: long_wildcard
  )EOF";
  loadConfig(yaml);

  // It should match the longer suffix wildcard (*.example.com) first.
  {
    auto input = TestDataInputStringFactory("api.example.com");
    validateMatch("long_wildcard"); // Longest suffix wins.
  }

  // It should match shorter wildcard when longer doesn't apply.
  {
    auto input = TestDataInputStringFactory("api.other.com");
    validateMatch("short_wildcard");
  }
}

TEST_F(DomainMatcherTest, GlobalWildcardMatching) {
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

  // Global wildcard should match any domain.
  {
    auto input = TestDataInputStringFactory("example.com");
    validateMatch("global_wildcard_match");
  }
  {
    auto input = TestDataInputStringFactory("api.example.com");
    validateMatch("global_wildcard_match");
  }
  {
    auto input = TestDataInputStringFactory("totally.different.org");
    validateMatch("global_wildcard_match");
  }
}

TEST_F(DomainMatcherTest, MultipleDomainsSingleMatcher) {
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
        - "web.example.com"
        - "*.example.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: example_group_match
      - domains:
        - "*.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: com_wildcard
  )EOF";
  loadConfig(yaml);

  // Test exact matches from the multi-domain matcher.
  {
    auto input = TestDataInputStringFactory("api.example.com");
    validateMatch("example_group_match");
  }
  {
    auto input = TestDataInputStringFactory("web.example.com");
    validateMatch("example_group_match");
  }

  // Test wildcard match from the multi-domain matcher.
  {
    auto input = TestDataInputStringFactory("other.example.com");
    validateMatch("example_group_match");
  }

  // Test fall-through to less specific matcher.
  {
    auto input = TestDataInputStringFactory("different.com");
    validateMatch("com_wildcard");
  }
}

TEST_F(DomainMatcherTest, CompleteWildcardPrecedence) {
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

  // Test exact match (highest priority).
  {
    auto input = TestDataInputStringFactory("exact.example.com");
    validateMatch("exact_match");
  }

  // Test specific wildcard match (middle priority).
  {
    auto input = TestDataInputStringFactory("test.example.com");
    validateMatch("specific_wildcard");
  }

  // Test global wildcard match (lowest priority).
  {
    auto input = TestDataInputStringFactory("something.else.com");
    validateMatch("global_wildcard");
  }
}

TEST_F(DomainMatcherTest, EmptyConfiguration) {
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

TEST_F(DomainMatcherTest, OnNoMatchHandler) {
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

TEST_F(DomainMatcherTest, DataAvailabilityHandling) {
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

TEST_F(DomainMatcherTest, InvalidInputType) {
  const std::string yaml = R"EOF(
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
  loadConfig(yaml);
  auto input = ::Envoy::Matcher::TestDataInputFloatFactory(3.14);
  const std::string error_message = "Unsupported data input type: float, currently only "
                                    "string type is supported in domain matcher";
  auto match_tree = factory_.create(matcher_);
  EXPECT_THROW_WITH_MESSAGE(match_tree(), EnvoyException, error_message);
}

TEST_F(DomainMatcherTest, InvalidDomainFormats) {
  // Test multiple wildcards.
  {
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
            - "*.*.example.com"
            on_match:
              action:
                name: test_action
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: invalid
    )EOF";
    loadConfig(yaml);
    EXPECT_THROW_WITH_MESSAGE(
        factory_.create(matcher_), EnvoyException,
        "Invalid wildcard domain format: *.*.example.com. Multiple wildcards are not supported");
  }

  // Test malformed wildcard.
  {
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
            - "*something.com"
            on_match:
              action:
                name: test_action
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: invalid
    )EOF";
    loadConfig(yaml);
    EXPECT_THROW_WITH_MESSAGE(factory_.create(matcher_), EnvoyException,
                              "Invalid wildcard domain format: *something.com. Only '*' and "
                              "'*.domain' patterns are supported");
  }

  // Test wildcard in middle of domain.
  {
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
            - "a.*"
            on_match:
              action:
                name: test_action
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: invalid
    )EOF";
    loadConfig(yaml);
    EXPECT_THROW_WITH_MESSAGE(
        factory_.create(matcher_), EnvoyException,
        "Invalid wildcard domain format: a.*. Only '*' and '*.domain' patterns are supported");
  }

  // Test duplicate domain.
  {
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
            - "example.com"
            on_match:
              action:
                name: test_action
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: duplicate
    )EOF";
    loadConfig(yaml);
    EXPECT_THROW_WITH_MESSAGE(factory_.create(matcher_), EnvoyException,
                              "Duplicate domain in ServerNameMatcher: example.com");
  }
}

TEST_F(DomainMatcherTest, KeepMatchingSupport) {
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
          keep_matching: true
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: keep_matching
      - domains:
        - "*.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: final_match
on_no_match:
  action:
    name: test_action
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: no_match
  )EOF";
  loadConfig(yaml);

  validation_visitor_.setSupportKeepMatching(true);
  std::vector<Envoy::Matcher::ActionConstSharedPtr> skipped_results;
  skipped_match_cb_ = [&skipped_results](const Envoy::Matcher::ActionConstSharedPtr& cb) {
    skipped_results.push_back(cb);
  };

  auto input = TestDataInputStringFactory("example.com");
  const auto result = doMatch();
  // With ``keep_matching=true``, exact match is skipped and wildcard match is used.
  EXPECT_THAT(result, HasStringAction("final_match"));
  EXPECT_THAT(skipped_results, ElementsAre(IsStringAction("keep_matching")));
}

// Test that demonstrates ServerNameMatcher supports nested matchers in on_match.
// This test shows that when a domain matches (e.g., "*.example.com"), the matcher
// can recursively evaluate a nested matcher based on a different input.
TEST_F(DomainMatcherTest, NestedMatcherSupport) {
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
        - "*.example.com"
        on_match:
          matcher:
            matcher_tree:
              input:
                name: nested_input
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.BoolValue
              exact_match_map:
                map:
                  "production":
                    action:
                      name: test_action
                      typed_config:
                        "@type": type.googleapis.com/google.protobuf.StringValue
                        value: nested_production_match
                  "staging":
                    action:
                      name: test_action
                      typed_config:
                        "@type": type.googleapis.com/google.protobuf.StringValue
                        value: nested_staging_match
            on_no_match:
              action:
                name: test_action
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: nested_default_match
      - domains:
        - "other.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: simple_match
on_no_match:
  action:
    name: test_action
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: no_domain_match
  )EOF";
  loadConfig(yaml);

  // Test domain matches and nested matcher evaluates successfully.
  {
    auto domain_input = TestDataInputStringFactory("api.example.com");
    auto nested_input = TestDataInputBoolFactory("production");
    validateMatch("nested_production_match");
  }

  // Test domain matches and different nested value.
  {
    auto domain_input = TestDataInputStringFactory("staging.example.com");
    auto nested_input = TestDataInputBoolFactory("staging");
    validateMatch("nested_staging_match");
  }

  // Test domain matches but nested matcher has no match (uses nested on_no_match).
  {
    auto domain_input = TestDataInputStringFactory("dev.example.com");
    auto nested_input = TestDataInputBoolFactory("unknown");
    validateMatch("nested_default_match");
  }

  // Test different domain that uses simple action with no nesting.
  {
    auto domain_input = TestDataInputStringFactory("other.com");
    auto nested_input = TestDataInputBoolFactory("production");
    validateMatch("simple_match");
  }

  // Test no domain match at all.
  {
    auto domain_input = TestDataInputStringFactory("unmatched.org");
    auto nested_input = TestDataInputBoolFactory("production");
    validateMatch("no_domain_match");
  }
}

// Test that when a more specific match (like "*.example.com") fails its inner condition,
// we should fall back to less specific matches (like "*.com" or "*") rather than giving
// up entirely.
TEST_F(DomainMatcherTest, BacktrackingWhenInnerConditionsFail) {
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
          matcher:
            matcher_tree:
              input:
                name: nested_input
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.BoolValue
              exact_match_map:
                map:
                  "fail_condition":
                    action:
                      name: test_action
                      typed_config:
                        "@type": type.googleapis.com/google.protobuf.StringValue
                        value: exact_inner_match
      - domains:
        - "*.example.com"
        on_match:
          matcher:
            matcher_tree:
              input:
                name: nested_input
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.BoolValue
              exact_match_map:
                map:
                  "fail_condition":
                    action:
                      name: test_action
                      typed_config:
                        "@type": type.googleapis.com/google.protobuf.StringValue
                        value: wildcard_specific_inner_match
      - domains:
        - "*.com"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: wildcard_broad_match
      - domains:
        - "*"
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: global_wildcard_match
on_no_match:
  action:
    name: test_action
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: no_match
  )EOF";
  loadConfig(yaml);

  // Test successful exact match with inner condition success.
  {
    auto domain_input = TestDataInputStringFactory("api.example.com");
    auto nested_input = TestDataInputBoolFactory("fail_condition");
    validateMatch("exact_inner_match");
  }

  // Test backtracking: exact match fails inner condition, falls back to wildcard match.
  {
    auto domain_input = TestDataInputStringFactory("api.example.com");
    auto nested_input =
        TestDataInputBoolFactory("different_condition"); // This will cause exact match to fail.
    validateMatch("wildcard_broad_match");               // Should backtrack to *.com
  }

  // Test backtracking: wildcard specific match fails, falls back to broader wildcard
  {
    auto domain_input = TestDataInputStringFactory("test.example.com");
    auto nested_input =
        TestDataInputBoolFactory("different_condition"); // This will cause *.example.com to fail.
    validateMatch("wildcard_broad_match");               // Should backtrack to *.com
  }

  // Test backtracking. All specific matches should fail, falling back to global wildcard.
  {
    auto domain_input = TestDataInputStringFactory("test.other.org");
    auto nested_input = TestDataInputBoolFactory("any_condition");
    validateMatch("global_wildcard_match"); // Should backtrack to *
  }

  // Test successful wildcard specific match with inner condition success.
  {
    auto domain_input = TestDataInputStringFactory("staging.example.com");
    auto nested_input = TestDataInputBoolFactory("fail_condition");
    validateMatch("wildcard_specific_inner_match");
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
  EXPECT_THAT(result, HasStringAction("matching"));
}

} // namespace
} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
