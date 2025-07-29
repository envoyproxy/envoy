#include <exception>
#include <memory>

#include "envoy/config/common/matcher/v3/matcher.pb.validate.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/registry/registry.h"

#include "source/common/matcher/list_matcher.h"
#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"

#include "test/common/matcher/test_utility.h"
#include "test/mocks/matcher/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "xds/type/matcher/v3/matcher.pb.validate.h"

namespace Envoy {
namespace Matcher {

using ::testing::ElementsAre;

class MatcherTest : public ::testing::Test {
public:
  MatcherTest()
      : inject_action_(action_factory_), factory_(context_, factory_context_, validation_visitor_) {
  }

  StringActionFactory action_factory_;
  Registry::InjectFactory<ActionFactory<absl::string_view>> inject_action_;
  MockMatchTreeValidationVisitor<TestData> validation_visitor_;

  absl::string_view context_ = "";
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  MatchTreeFactory<TestData, absl::string_view> factory_;
};

TEST_F(MatcherTest, TestMatcher) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: outer_input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  exact_match_map:
    map:
      value:
        matcher:
          matcher_list:
            matchers:
            - on_match:
                action:
                  name: test_action
                  typed_config:
                    "@type": type.googleapis.com/google.protobuf.StringValue
                    value: expected!
              predicate:
                single_predicate:
                  input:
                    name: inner_input
                    typed_config:
                      "@type": type.googleapis.com/google.protobuf.BoolValue
                  value_match:
                    exact: foo
  )EOF";

  envoy::config::common::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);

  auto outer_factory = TestDataInputStringFactory("value");
  auto inner_factory = TestDataInputBoolFactory("foo");

  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.StringValue"));
  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.BoolValue"));
  auto match_tree = factory_.create(matcher);

  MatchResult result = evaluateMatch(*match_tree(), TestData());
  EXPECT_THAT(result, HasStringAction("expected!"));
}

TEST_F(MatcherTest, TestPrefixMatcher) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: outer_input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  prefix_match_map:
    map:
      val:
        matcher:
          matcher_list:
            matchers:
            - on_match:
                action:
                  name: test_action
                  typed_config:
                    "@type": type.googleapis.com/google.protobuf.StringValue
                    value: expected!
              predicate:
                single_predicate:
                  input:
                    name: inner_input
                    typed_config:
                      "@type": type.googleapis.com/google.protobuf.BoolValue
                  value_match:
                    exact: foo
  )EOF";

  envoy::config::common::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);

  auto outer_factory = TestDataInputStringFactory("value");
  auto inner_factory = TestDataInputBoolFactory("foo");

  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.StringValue"));
  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.BoolValue"));
  auto match_tree = factory_.create(matcher);

  MatchResult result = evaluateMatch(*match_tree(), TestData());
  EXPECT_THAT(result, HasStringAction("expected!"));
}

TEST_F(MatcherTest, TestPrefixMatcherWithRetryInnerMissPerformsOuterOnNoMatch) {
  const std::string yaml = R"EOF(
on_no_match:
  action:
    name: test_action
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: expected!
matcher_tree:
  input:
    name: outer_input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  prefix_match_map:
    map:
      "":
        matcher:
          matcher_list:
            matchers:
            - predicate:
                single_predicate:
                  input:
                    name: inner_input
                    typed_config:
                      "@type": type.googleapis.com/google.protobuf.BoolValue
                  value_match:
                    exact: foo
              on_match:
                action:
                  name: test_action
                  typed_config:
                    "@type": type.googleapis.com/google.protobuf.StringValue
                    value: unexpected
      val:
        matcher:
          matcher_list:
            matchers:
            - predicate:
                single_predicate:
                  input:
                    name: inner_input
                    typed_config:
                      "@type": type.googleapis.com/google.protobuf.BoolValue
                  value_match:
                    exact: foo
              on_match:
                action:
                  name: test_action
                  typed_config:
                    "@type": type.googleapis.com/google.protobuf.StringValue
                    value: unexpected
  )EOF";

  envoy::config::common::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);

  auto outer_factory = TestDataInputStringFactory("value");
  auto inner_factory = TestDataInputBoolFactory("bar");

  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.StringValue"));
  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.BoolValue"))
      .Times(2);
  auto match_tree = factory_.create(matcher);

  const auto result = match_tree()->match(TestData());
  EXPECT_THAT(result, HasStringAction("expected!"));
}

TEST_F(MatcherTest, TestPrefixMatcherWithRetryInnerMissRetriesShorterPrefix) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: outer_input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  prefix_match_map:
    map:
      val:
        matcher:
          matcher_list:
            matchers:
            - predicate:
                single_predicate:
                  input:
                    name: inner_input
                    typed_config:
                      "@type": type.googleapis.com/google.protobuf.BoolValue
                  value_match:
                    exact: bar
              on_match:
                action:
                  name: test_action
                  typed_config:
                    "@type": type.googleapis.com/google.protobuf.StringValue
                    value: expected!
      valu:
        matcher:
          matcher_list:
            matchers:
            - predicate:
                single_predicate:
                  input:
                    name: inner_input
                    typed_config:
                      "@type": type.googleapis.com/google.protobuf.BoolValue
                  value_match:
                    exact: foo
              on_match:
                action:
                  name: test_action
                  typed_config:
                    "@type": type.googleapis.com/google.protobuf.StringValue
                    value: not expected
  )EOF";

  envoy::config::common::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);

  auto outer_factory = TestDataInputStringFactory("value");
  auto inner_factory = TestDataInputBoolFactory("bar");

  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.StringValue"));
  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.BoolValue"))
      .Times(2);
  auto match_tree = factory_.create(matcher);

  const auto result = match_tree()->match(TestData());
  EXPECT_THAT(result, HasStringAction("expected!"));
}

TEST_F(MatcherTest, TestPrefixMatcherWithoutRetryInnerMissDoesNotRetryShorterPrefix) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.prefix_map_matcher_resume_after_subtree_miss", "false"}});
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: outer_input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  prefix_match_map:
    map:
      val:
        matcher:
          matcher_list:
            matchers:
            - predicate:
                single_predicate:
                  input:
                    name: inner_input
                    typed_config:
                      "@type": type.googleapis.com/google.protobuf.BoolValue
                  value_match:
                    exact: bar
              on_match:
                action:
                  name: test_action
                  typed_config:
                    "@type": type.googleapis.com/google.protobuf.StringValue
                    value: expected!
      valu:
        matcher:
          matcher_list:
            matchers:
            - predicate:
                single_predicate:
                  input:
                    name: inner_input
                    typed_config:
                      "@type": type.googleapis.com/google.protobuf.BoolValue
                  value_match:
                    exact: foo
              on_match:
                action:
                  name: test_action
                  typed_config:
                    "@type": type.googleapis.com/google.protobuf.StringValue
                    value: not expected
  )EOF";

  envoy::config::common::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);

  auto outer_factory = TestDataInputStringFactory("value");
  auto inner_factory = TestDataInputBoolFactory("bar");

  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.StringValue"));
  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.BoolValue"))
      .Times(2);
  auto match_tree = factory_.create(matcher);

  const auto result = match_tree()->match(TestData());
  EXPECT_THAT(result, HasNoMatch());
}

TEST_F(MatcherTest, TestInvalidFloatPrefixMapMatcher) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: outer_input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.FloatValue
  prefix_match_map:
    map:
      3.14:
        matcher:
          matcher_list:
            matchers:
            - on_match:
                action:
                  name: test_action
                  typed_config:
                    "@type": type.googleapis.com/google.protobuf.StringValue
                    value: not expected
              predicate:
                single_predicate:
                  input:
                    name: inner_input
                    typed_config:
                      "@type": type.googleapis.com/google.protobuf.BoolValue
                  value_match:
                    exact: foo
  )EOF";

  envoy::config::common::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);
  auto outer_input_factory = TestDataInputFloatFactory(3.14);
  auto inner_input_factory = TestDataInputBoolFactory("foo");

  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.BoolValue"));
  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.FloatValue"));

  auto match_tree = factory_.create(matcher);
  std::string error_message = absl::StrCat(
      "Unsupported data input type: float, currently only string type is supported in map matcher");

  EXPECT_THROW_WITH_MESSAGE(match_tree(), EnvoyException, error_message);
}

TEST_F(MatcherTest, TestInvalidFloatExactMapMatcher) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: outer_input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.FloatValue
  exact_match_map:
    map:
      3.14:
        matcher:
          matcher_list:
            matchers:
            - on_match:
                action:
                  name: test_action
                  typed_config:
                    "@type": type.googleapis.com/google.protobuf.StringValue
                    value: not expected
              predicate:
                single_predicate:
                  input:
                    name: inner_input
                    typed_config:
                      "@type": type.googleapis.com/google.protobuf.BoolValue
                  value_match:
                    exact: foo
  )EOF";

  envoy::config::common::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);
  auto outer_input_factory = TestDataInputFloatFactory(3.14);
  auto inner_input_factory = TestDataInputBoolFactory("foo");

  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.BoolValue"));
  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.FloatValue"));
  auto match_tree = factory_.create(matcher);
  std::string error_message = absl::StrCat(
      "Unsupported data input type: float, currently only string type is supported in map matcher");
  EXPECT_THROW_WITH_MESSAGE(match_tree(), EnvoyException, error_message);
}

TEST_F(MatcherTest, InvalidDataInput) {
  const std::string yaml = R"EOF(
matcher_list:
  matchers:
  - on_match:
      action:
        name: test_action
        typed_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
          value: not expected
    predicate:
      single_predicate:
        input:
          name: generic
          typed_config:
            "@type": type.googleapis.com/google.protobuf.FloatValue
        value_match:
          exact: 3.14

  )EOF";
  envoy::config::common::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);

  auto outer_input_factory = TestDataInputFloatFactory(3.14);

  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.FloatValue"));
  auto match_tree = factory_.create(matcher);
  std::string error_message = absl::StrCat("Unsupported data input type: float.",
                                           " The matcher supports input type: string");
  EXPECT_THROW_WITH_MESSAGE(match_tree(), EnvoyException, error_message);
}

TEST_F(MatcherTest, InvalidDataInputInAndMatcher) {
  const std::string yaml = R"EOF(
  matcher_list:
    matchers:
    - on_match:
        action:
          name: test_action
          typed_config:
            "@type": type.googleapis.com/google.protobuf.StringValue
            value: not expected
      predicate:
        and_matcher:
          predicate:
          - single_predicate:
              input:
                name: inner_input
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.FloatValue
              value_match:
                exact: 3.14
          - single_predicate:
              input:
                name: inner_input
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.FloatValue
              value_match:
                exact: 3.14

  )EOF";
  envoy::config::common::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);

  auto outer_input_factory = TestDataInputFloatFactory(3.14);

  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.FloatValue"))
      .Times(2);

  std::string error_message = absl::StrCat("Unsupported data input type: float.",
                                           " The matcher supports input type: string");
  EXPECT_THROW_WITH_MESSAGE(factory_.create(matcher)(), EnvoyException, error_message);
}

TEST_F(MatcherTest, TestAnyMatcher) {
  const std::string yaml = R"EOF(
on_no_match:
  action:
    name: test_action
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: expected!
  )EOF";

  xds::type::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);

  auto match_tree = factory_.create(matcher);

  MatchResult result = evaluateMatch(*match_tree(), TestData());
  EXPECT_THAT(result, HasStringAction("expected!"));
}

TEST_F(MatcherTest, CustomGenericInput) {
  const std::string yaml = R"EOF(
matcher_list:
  matchers:
  - on_match:
      action:
        name: test_action
        typed_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
          value: expected!
    predicate:
      single_predicate:
        input:
          name: generic
          typed_config:
            "@type": type.googleapis.com/google.protobuf.StringValue
        value_match:
          exact: foo

  )EOF";
  envoy::config::common::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);

  auto common_input_factory = TestCommonProtocolInputFactory("generic", "foo");
  auto match_tree = factory_.create(matcher);

  MatchResult result = evaluateMatch(*match_tree(), TestData());
  EXPECT_THAT(result, HasStringAction("expected!"));
}

TEST_F(MatcherTest, CustomMatcher) {
  const std::string yaml = R"EOF(
matcher_list:
  matchers:
  - on_match:
      action:
        name: test_action
        typed_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
          value: expected!
    predicate:
      single_predicate:
        input:
          name: inner_input
          typed_config:
            "@type": type.googleapis.com/google.protobuf.BoolValue
        custom_match:
          name: custom_match
          typed_config:
            "@type": type.googleapis.com/google.protobuf.StringValue
            value: custom_foo
  )EOF";

  envoy::config::common::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);

  // Build the input data that is to be matched.
  std::string value = "custom_foo";
  auto inner_factory = TestDataInputBoolFactory(value);

  // Register the custom matcher factory to perform the matching.
  CustomStringMatcherFactory custom_factory;

  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.BoolValue"));
  auto match_tree = factory_.create(matcher);

  MatchResult result = evaluateMatch(*match_tree(), TestData());
  EXPECT_THAT(result, HasStringAction("expected!"));
}

TEST_F(MatcherTest, TestAndMatcher) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: outer_input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  exact_match_map:
    map:
      value:
        matcher:
          matcher_list:
            matchers:
            - on_match:
                action:
                  name: test_action
                  typed_config:
                    "@type": type.googleapis.com/google.protobuf.StringValue
                    value: expected!
              predicate:
                and_matcher:
                  predicate:
                  - single_predicate:
                      input:
                        name: inner_input
                        typed_config:
                          "@type": type.googleapis.com/google.protobuf.BoolValue
                      value_match:
                        exact: foo
                  - single_predicate:
                      input:
                        name: inner_input
                        typed_config:
                          "@type": type.googleapis.com/google.protobuf.BoolValue
                      value_match:
                        exact: foo
  )EOF";

  envoy::config::common::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);

  auto outer_factory = TestDataInputStringFactory("value");
  auto inner_factory = TestDataInputBoolFactory("foo");

  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.StringValue"));
  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.BoolValue"))
      .Times(2);
  auto match_tree = factory_.create(matcher);

  MatchResult result = evaluateMatch(*match_tree(), TestData());
  EXPECT_THAT(result, HasStringAction("expected!"));
}

TEST_F(MatcherTest, TestOrMatcher) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: outer_input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  exact_match_map:
    map:
      value:
        matcher:
          matcher_list:
            matchers:
            - on_match:
                action:
                  name: test_action
                  typed_config:
                    "@type": type.googleapis.com/google.protobuf.StringValue
                    value: expected!
              predicate:
                or_matcher:
                  predicate:
                  - single_predicate:
                      input:
                        name: inner_input
                        typed_config:
                          "@type": type.googleapis.com/google.protobuf.BoolValue
                      value_match:
                        exact: bar
                  - single_predicate:
                      input:
                        name: inner_input
                        typed_config:
                          "@type": type.googleapis.com/google.protobuf.BoolValue
                      value_match:
                        exact: foo
  )EOF";

  envoy::config::common::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);

  auto outer_factory = TestDataInputStringFactory("value");
  auto inner_factory = TestDataInputBoolFactory("foo");

  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.StringValue"));
  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.BoolValue"))
      .Times(2);
  auto match_tree = factory_.create(matcher);

  MatchResult result = evaluateMatch(*match_tree(), TestData());
  EXPECT_THAT(result, HasStringAction("expected!"));
}

TEST_F(MatcherTest, TestNotMatcher) {
  const std::string yaml = R"EOF(
matcher_list:
  matchers:
  - on_match:
      action:
        name: test_action
        typed_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
          value: not expected
    predicate:
      not_matcher:
        single_predicate:
          input:
            name: inner_input
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
          value_match:
            exact: foo
  )EOF";

  envoy::config::common::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);

  auto inner_factory = TestDataInputStringFactory("foo");
  NeverMatchFactory match_factory;

  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.StringValue"));
  auto match_tree = factory_.create(matcher);

  MatchResult result = evaluateMatch(*match_tree(), TestData());
  EXPECT_THAT(result, HasNoMatch());
}

TEST_F(MatcherTest, TestRecursiveMatcher) {
  const std::string yaml = R"EOF(
matcher_list:
  matchers:
  - on_match:
      matcher:
        matcher_list:
          matchers:
          - on_match:
              action:
                name: test_action
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: expected!
            predicate:
              single_predicate:
                input:
                  name: inner_input
                  typed_config:
                    "@type": type.googleapis.com/google.protobuf.BoolValue
                value_match:
                  exact: foo
    predicate:
      single_predicate:
        input:
          name: inner_input
          typed_config:
            "@type": type.googleapis.com/google.protobuf.BoolValue
        value_match:
          exact: foo
  )EOF";

  envoy::config::common::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);

  auto outer_factory = TestDataInputStringFactory("value");
  auto inner_factory = TestDataInputBoolFactory("foo");

  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.BoolValue"))
      .Times(2);
  auto match_tree = factory_.create(matcher);

  // Show that a single match() call handles sub-matching internally.
  const auto result = match_tree()->match(TestData());
  EXPECT_THAT(result, HasStringAction("expected!"));
}

TEST_F(MatcherTest, RecursiveMatcherNoMatch) {
  ListMatcher<TestData> matcher(absl::nullopt);

  matcher.addMatcher(createSingleMatcher(absl::nullopt, [](auto) { return false; }),
                     stringOnMatch<TestData>("match"));

  MatchResult recursive_result = evaluateMatch(matcher, TestData());
  EXPECT_THAT(recursive_result, HasNoMatch());
}

TEST_F(MatcherTest, RecursiveMatcherCannotMatch) {
  ListMatcher<TestData> matcher(absl::nullopt);

  matcher.addMatcher(createSingleMatcher(
                         absl::nullopt, [](auto) { return false; },
                         DataInputGetResult::DataAvailability::NotAvailable),
                     stringOnMatch<TestData>("match"));

  MatchResult recursive_result = evaluateMatch(matcher, TestData());
  EXPECT_THAT(recursive_result, HasInsufficientData());
}

// Parameterized to test both xDS and Envoy Matcher APIs for new features.
class MatcherAmbiguousTest : public MatcherTest, public ::testing::WithParamInterface<bool> {
public:
  // Exercise both xDS and Envoy Matcher APIs, based on the test parameter.
  MatchTreeFactoryCb<TestData> createMatcherFromYaml(const std::string& yaml) {
    if (GetParam()) {
      xds::type::matcher::v3::Matcher xds_matcher;
      MessageUtil::loadFromYaml(yaml, xds_matcher, ProtobufMessage::getStrictValidationVisitor());
      EXPECT_TRUE(validation_visitor_.errors().empty());
      TestUtility::validate(xds_matcher);
      return factory_.create(xds_matcher);
    } else {
      envoy::config::common::matcher::v3::Matcher envoy_matcher;
      MessageUtil::loadFromYaml(yaml, envoy_matcher, ProtobufMessage::getStrictValidationVisitor());
      EXPECT_TRUE(validation_visitor_.errors().empty());
      TestUtility::validate(envoy_matcher);
      return factory_.create(envoy_matcher);
    }
  }
};
INSTANTIATE_TEST_SUITE_P(UseXdsMatcherType, MatcherAmbiguousTest, ::testing::Bool());

// Test that keep_matching is supported via re-entry, even in evaluateMatch(...), that callers
// cannot re-enter afterwards.
TEST_P(MatcherAmbiguousTest, KeepMatchingSupportInEvaluation) {
  const std::string yaml = R"EOF(
    matcher_list:
      matchers:
      - on_match:
          matcher:
            matcher_list:
              matchers:
              - on_match:
                  keep_matching: true
                  action:
                    name: test_action
                    typed_config:
                      "@type": type.googleapis.com/google.protobuf.StringValue
                      value: keep-matching
                predicate:
                  single_predicate:
                    input:
                      name: inner_input
                      typed_config:
                        "@type": type.googleapis.com/google.protobuf.BoolValue
                    value_match:
                      exact: foo
        predicate:
          single_predicate:
            input:
              name: inner_input
              typed_config:
                "@type": type.googleapis.com/google.protobuf.StringValue
            value_match:
              exact: value
    on_no_match:
      action:
        name: test_action
        typed_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
          value: on-no-match
      )EOF";

  auto outer_factory = TestDataInputStringFactory("value");
  auto inner_factory = TestDataInputBoolFactory("foo");

  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.BoolValue"));
  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.StringValue"));
  validation_visitor_.setSupportKeepMatching(true);
  std::shared_ptr<MatchTree<TestData>> matcher = createMatcherFromYaml(yaml)();

  std::vector<ActionConstSharedPtr> skipped_results;
  SkippedMatchCb skipped_match_cb = [&skipped_results](ActionConstSharedPtr cb) {
    skipped_results.push_back(cb);
  };
  const auto result = evaluateMatch(*matcher, TestData(), skipped_match_cb);
  EXPECT_THAT(result, HasStringAction("on-no-match"));
  EXPECT_THAT(skipped_results, ElementsAre(IsStringAction("keep-matching")));
}

TEST_P(MatcherAmbiguousTest, KeepMatchingWithRecursiveMatcher) {
  const std::string yaml = R"EOF(
    matcher_list:
      matchers:
      - on_match:
          keep_matching: true
          matcher:
            matcher_list:
              matchers:
              - on_match:
                  action:
                    name: test_action
                    typed_config:
                      "@type": type.googleapis.com/google.protobuf.StringValue
                      value: no-match-1
                predicate:
                  single_predicate:
                    input:
                      name: inner_input
                      typed_config:
                        "@type": type.googleapis.com/google.protobuf.StringValue
                    value_match:
                      exact: bar
              - on_match:
                  keep_matching: true
                  action:
                    name: test_action
                    typed_config:
                      "@type": type.googleapis.com/google.protobuf.StringValue
                      value: nested-keep-matching-1
                predicate:
                  single_predicate:
                    input:
                      name: inner_input
                      typed_config:
                        "@type": type.googleapis.com/google.protobuf.StringValue
                    value_match:
                      exact: foo
            on_no_match:
              action:
                name: test_action
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: on-no-match-nested-1
        predicate:
          single_predicate:
            input:
              name: inner_input
              typed_config:
                "@type": type.googleapis.com/google.protobuf.StringValue
            value_match:
              exact: foo
      - on_match:
          matcher:
            matcher_list:
              matchers:
              - on_match:
                  keep_matching: true
                  action:
                    name: test_action
                    typed_config:
                      "@type": type.googleapis.com/google.protobuf.StringValue
                      value: nested-keep-matching-2
                predicate:
                  single_predicate:
                    input:
                      name: inner_input
                      typed_config:
                        "@type": type.googleapis.com/google.protobuf.StringValue
                    value_match:
                      exact: foo
              - on_match:
                  action:
                    name: test_action
                    typed_config:
                      "@type": type.googleapis.com/google.protobuf.StringValue
                      value: nested-match-2
                predicate:
                  single_predicate:
                    input:
                      name: inner_input
                      typed_config:
                        "@type": type.googleapis.com/google.protobuf.StringValue
                    value_match:
                      exact: foo
        predicate:
          single_predicate:
            input:
              name: inner_input
              typed_config:
                "@type": type.googleapis.com/google.protobuf.StringValue
            value_match:
              exact: foo
      )EOF";

  auto inner_factory = TestDataInputStringFactory("foo");
  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.StringValue"))
      .Times(6);
  validation_visitor_.setSupportKeepMatching(true);
  std::shared_ptr<MatchTree<TestData>> matcher = createMatcherFromYaml(yaml)();

  // Expect the nested matchers with keep_matching to be skipped and also the top-level
  // keep_matching setting to skip the result of the first sub-matcher.
  std::vector<ActionConstSharedPtr> skipped_results;
  SkippedMatchCb skipped_match_cb = [&skipped_results](ActionConstSharedPtr cb) {
    skipped_results.push_back(cb);
  };
  MatchResult result = evaluateMatch(*matcher, TestData(), skipped_match_cb);
  EXPECT_THAT(result, HasStringAction(("nested-match-2")));
  EXPECT_THAT(skipped_results, ElementsAre(IsStringAction("nested-keep-matching-1"),
                                           IsStringAction("on-no-match-nested-1"),
                                           IsStringAction("nested-keep-matching-2")));
}

TEST_P(MatcherAmbiguousTest, KeepMatchingWithUnsupportedReentry) {
  // ExactMapMatcher cannot give a second match, so keep_matching results in a no-match.
  const std::string yaml = R"EOF(
    matcher_tree:
      input:
        name: inner_input
        typed_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
      exact_match_map:
        map:
          foo:
            keep_matching: true
            action:
              name: test_action
              typed_config:
                "@type": type.googleapis.com/google.protobuf.StringValue
                value: keep matching
      )EOF";

  auto inner_factory = TestDataInputStringFactory("foo");
  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.StringValue"));
  validation_visitor_.setSupportKeepMatching(true);
  std::shared_ptr<MatchTree<TestData>> matcher = createMatcherFromYaml(yaml)();

  std::vector<ActionConstSharedPtr> skipped_results;
  SkippedMatchCb skipped_match_cb = [&skipped_results](ActionConstSharedPtr cb) {
    skipped_results.push_back(cb);
  };
  MatchResult result = evaluateMatch(*matcher, TestData(), skipped_match_cb);
  EXPECT_THAT(result, HasNoMatch());
  EXPECT_THAT(skipped_results, ElementsAre(IsStringAction("keep matching")));
}

TEST_P(MatcherAmbiguousTest, KeepMatchingWithoutSupport) {
  // Expect a failure during config validation when keep_matching is set but not supported.
  const std::string yaml = R"EOF(
    matcher_tree:
      input:
        name: inner_input
        typed_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
      exact_match_map:
        map:
          foo:
            matcher:
              matcher_list:
                matchers:
                - on_match:
                    keep_matching: true
                    action:
                      name: test_action
                      typed_config:
                        "@type": type.googleapis.com/google.protobuf.StringValue
                        value: keep matching
                  predicate:
                    single_predicate:
                      input:
                        name: inner_input
                        typed_config:
                          "@type": type.googleapis.com/google.protobuf.BoolValue
                      value_match:
                        exact: bar
          bat:
            action:
              name: test_action
              typed_config:
                "@type": type.googleapis.com/google.protobuf.StringValue
                value: bat
      )EOF";

  auto outer_factory = TestDataInputStringFactory("value");
  auto inner_factory = TestDataInputBoolFactory("foo");
  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.StringValue"));
  EXPECT_CALL(validation_visitor_,
              performDataInputValidation(_, "type.googleapis.com/google.protobuf.BoolValue"));
  validation_visitor_.setSupportKeepMatching(false);

  MatchTreeFactoryCb<TestData> matcher_creator = createMatcherFromYaml(yaml);
  ASSERT_FALSE(validation_visitor_.errors().empty());
  EXPECT_EQ(validation_visitor_.errors()[0].message(),
            "keep_matching is not supported in this context");
}

// Ensure that a nested matcher that has an internal failure surfaces the error.
TEST_P(MatcherAmbiguousTest, KeepMatchingWithFailingNestedMatcher) {
  auto matcher = std::make_shared<ListMatcher<TestData>>(absl::nullopt);

  matcher->addMatcher(createSingleMatcher("string", [](auto) { return true; }),
                      stringOnMatch<TestData>("match", /*keep_matching=*/true));

  auto nested_matcher = std::make_shared<ListMatcher<TestData>>(absl::nullopt);
  nested_matcher->addMatcher(
      createSingleMatcher(
          "string", [](auto) { return true; }, DataInputGetResult::DataAvailability::NotAvailable),
      stringOnMatch<TestData>("fail"));

  matcher->addMatcher(createSingleMatcher("string", [](auto) { return true; }),
                      OnMatch<TestData>{/*.action_=*/nullptr, /*.matcher=*/nested_matcher,
                                        /*.keep_matching=*/true});

  // Expect re-entry to fail due to the nested matcher.
  std::vector<ActionConstSharedPtr> skipped_results;
  SkippedMatchCb skipped_match_cb = [&skipped_results](ActionConstSharedPtr cb) {
    skipped_results.push_back(cb);
  };
  MatchResult result = evaluateMatch(*matcher, TestData(), skipped_match_cb);
  EXPECT_THAT(result, HasInsufficientData());
  EXPECT_THAT(skipped_results, ElementsAre(IsStringAction("match")));
}

} // namespace Matcher
} // namespace Envoy
