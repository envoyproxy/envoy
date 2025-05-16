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
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "xds/type/matcher/v3/matcher.pb.validate.h"

namespace Envoy {
namespace Matcher {
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

MATCHER_P(IsStringAction, m, "") {
  // Accepts an ActionFactoryCb argument.
  if (arg == nullptr) {
    *result_listener << "action callback is nullptr";
    return false;
  }
  ActionPtr action = arg();
  StringAction string_action = action->getTyped<StringAction>();
  return ::testing::ExplainMatchResult(m, string_action.string_, result_listener);
}

MATCHER_P(HasStringAction, m, "") {
  // Accepts a MatchResult argument.
  if (arg.match_state_ != MatchState::MatchComplete) {
    *result_listener << "match_state_ is not MatchComplete";
    return false;
  }
  if (arg.on_match_ == absl::nullopt) {
    *result_listener << "on_match_ is nullopt";
    return false;
  }
  return ExplainMatchResult(IsStringAction(m), arg.on_match_->action_cb_, result_listener);
}

MATCHER(HasNoMatch, "") {
  // Accepts a MatchResult argument.
  if (arg.match_state_ != MatchState::MatchComplete) {
    *result_listener << "match_state_ is not MatchComplete";
    return false;
  }
  if (arg.on_match_ != absl::nullopt) {
    *result_listener << "on_match_ was not nullopt";
    return false;
  }
  return true;
}

MATCHER(HasSubMatcher, "") {
  // Accepts a MatchResult argument.
  if (arg.match_state_ != MatchState::MatchComplete) {
    *result_listener << "match_state_ is not MatchComplete";
    return false;
  }
  if (arg.on_match_ == absl::nullopt) {
    *result_listener << "on_match_ is nullopt";
    return false;
  }
  if (arg.on_match_->matcher_ == nullptr) {
    *result_listener << "on_match_->matcher_ is nullptr, expected it to not be.";
    if (arg.on_match_->action_cb_ != nullptr) {
      *result_listener << "\non_match_->action_cb_ is not nullptr.";
    }
    return false;
  }
  return true;
}

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

  const auto result = match_tree()->match(TestData());
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

  const auto result = match_tree()->match(TestData());
  EXPECT_THAT(result, HasStringAction("expected!"));
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

  const auto result = match_tree()->match(TestData());
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

  const auto result = match_tree()->match(TestData());
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

  const auto result = match_tree()->match(TestData());
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

  const auto result = match_tree()->match(TestData());
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

  const auto result = match_tree()->match(TestData());
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

  const auto result = match_tree()->match(TestData());
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

  const auto result = match_tree()->match(TestData());
  EXPECT_THAT(result, HasSubMatcher());

  const auto recursive_result = evaluateMatch(*(match_tree()), TestData());
  EXPECT_EQ(recursive_result.match_state_, MatchState::MatchComplete);
  EXPECT_THAT(recursive_result.result_, IsStringAction("expected!"));
}

TEST_F(MatcherTest, RecursiveMatcherNoMatch) {
  ListMatcher<TestData> matcher(absl::nullopt);

  matcher.addMatcher(createSingleMatcher(absl::nullopt, [](auto) { return false; }),
                     stringOnMatch<TestData>("match"));

  const auto recursive_result = evaluateMatch(matcher, TestData());
  EXPECT_EQ(recursive_result.match_state_, MatchState::MatchComplete);
  EXPECT_EQ(recursive_result.result_, nullptr);
}

TEST_F(MatcherTest, RecursiveMatcherCannotMatch) {
  ListMatcher<TestData> matcher(absl::nullopt);

  matcher.addMatcher(createSingleMatcher(
                         absl::nullopt, [](auto) { return false; },
                         DataInputGetResult::DataAvailability::NotAvailable),
                     stringOnMatch<TestData>("match"));

  const auto recursive_result = evaluateMatch(matcher, TestData());
  EXPECT_EQ(recursive_result.match_state_, MatchState::UnableToMatch);
  EXPECT_EQ(recursive_result.result_, nullptr);
}
} // namespace Matcher
} // namespace Envoy
