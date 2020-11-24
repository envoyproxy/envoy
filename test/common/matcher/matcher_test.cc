#include <exception>
#include <memory>

#include "envoy/config/common/matcher/v3/matcher.pb.validate.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/registry/registry.h"

#include "common/matcher/matcher.h"
#include "common/protobuf/utility.h"

#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {

struct TestData {};

class TestDataInput : public DataInput<TestData> {
public:
  explicit TestDataInput(const std::string& value) : value_(value) {}

  DataInputGetResult get(const TestData&) override {
    return {DataInputGetResult::DataAvailability::AllDataAvailable, value_};
  }

private:
  const std::string value_;
};

class TestDataInputFactory : public DataInputFactory<TestData> {
public:
  TestDataInputFactory(absl::string_view factory_name, absl::string_view data)
      : factory_name_(std::string(factory_name)), value_(std::string(data)), injection_(*this) {}

  DataInputPtr<TestData> createDataInput(const Protobuf::Message&) override {
    return std::make_unique<TestDataInput>(value_);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }
  std::string name() const override { return factory_name_; }

private:
  const std::string factory_name_;
  const std::string value_;
  Registry::InjectFactory<DataInputFactory<TestData>> injection_;
};

class TestAction : public ActionBase<ProtobufWkt::StringValue> {};

class TestActionFactory : public ActionFactory {
public:
  ActionFactoryCb createActionFactoryCb(const Protobuf::Message&) override {
    return []() { return std::make_unique<TestAction>(); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }
  std::string name() const override { return "test_action"; }
};

class NeverMatch : public InputMatcher {
public:
  bool match(absl::optional<absl::string_view>) override { return false; }
};

class NeverMatchFactory : public InputMatcherFactory {
public:
  NeverMatchFactory() : inject_factory_(*this) {}

  InputMatcherPtr createInputMatcher(const Protobuf::Message&) override {
    return std::make_unique<NeverMatch>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }

  std::string name() const override { return "never_match"; }

  Registry::InjectFactory<InputMatcherFactory> inject_factory_;
};

class MatcherTest : public ::testing::Test {
public:
  MatcherTest() : inject_action_(action_factory_) {}

  TestActionFactory action_factory_;
  Registry::InjectFactory<ActionFactory> inject_action_;
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
                    value: match!!
              predicate:
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

  MatchTreeFactory<TestData> factory(ProtobufMessage::getStrictValidationVisitor());

  auto outer_factory = TestDataInputFactory("outer_input", "value");
  auto inner_factory = TestDataInputFactory("inner_input", "foo");

  auto match_tree = factory.create(matcher);

  const auto result = match_tree->match(TestData());
  EXPECT_TRUE(result.match_completed_);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
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
          value: match!!
    predicate:
      single_predicate:
        input:
          name: inner_input
          typed_config:
            "@type": type.googleapis.com/google.protobuf.StringValue
        custom_match:
          name: never_match
          typed_config: {}
  )EOF";

  envoy::config::common::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);

  MatchTreeFactory<TestData> factory(ProtobufMessage::getStrictValidationVisitor());

  auto inner_factory = TestDataInputFactory("inner_input", "foo");
  NeverMatchFactory match_factory;

  auto match_tree = factory.create(matcher);

  const auto result = match_tree->match(TestData());
  EXPECT_TRUE(result.match_completed_);
  EXPECT_FALSE(result.on_match_.has_value());
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
                    value: match!!
              predicate:
                and_matcher:
                  predicate:
                  - single_predicate:
                      input:
                        name: inner_input
                        typed_config:
                          "@type": type.googleapis.com/google.protobuf.StringValue
                      value_match:
                        exact: foo
                  - single_predicate:
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

  MatchTreeFactory<TestData> factory(ProtobufMessage::getStrictValidationVisitor());

  auto outer_factory = TestDataInputFactory("outer_input", "value");
  auto inner_factory = TestDataInputFactory("inner_input", "foo");

  auto match_tree = factory.create(matcher);

  const auto result = match_tree->match(TestData());
  EXPECT_TRUE(result.match_completed_);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
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
                    value: match!!
              predicate:
                or_matcher:
                  predicate:
                  - single_predicate:
                      input:
                        name: inner_input
                        typed_config:
                          "@type": type.googleapis.com/google.protobuf.StringValue
                      value_match:
                        exact: foo
                  - single_predicate:
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

  MatchTreeFactory<TestData> factory(ProtobufMessage::getStrictValidationVisitor());

  auto outer_factory = TestDataInputFactory("outer_input", "value");
  auto inner_factory = TestDataInputFactory("inner_input", "foo");

  auto match_tree = factory.create(matcher);

  const auto result = match_tree->match(TestData());
  EXPECT_TRUE(result.match_completed_);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_NE(result.on_match_->action_cb_, nullptr);
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
                  value: match!!
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

  envoy::config::common::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  TestUtility::validate(matcher);

  MatchTreeFactory<TestData> factory(ProtobufMessage::getStrictValidationVisitor());

  auto outer_factory = TestDataInputFactory("outer_input", "value");
  auto inner_factory = TestDataInputFactory("inner_input", "foo");

  auto match_tree = factory.create(matcher);

  const auto result = match_tree->match(TestData());
  EXPECT_TRUE(result.match_completed_);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_EQ(result.on_match_->action_cb_, nullptr);

  const auto recursive_result = evaluateMatch(*match_tree, TestData());
  EXPECT_TRUE(recursive_result.final_);
  EXPECT_NE(recursive_result.result_, nullptr);
}
} // namespace Matcher
} // namespace Envoy