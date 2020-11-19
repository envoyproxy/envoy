#include <memory>

#include "envoy/config/common/matcher/v3/matcher.pb.validate.h"
#include "envoy/config/core/v3/extension.pb.h"

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

  virtual DataInputGetResult get(const TestData&) override {
    return {DataInputGetResult::DataAvailability::AllDataAvailable, value_};
  }

private:
  const std::string value_;
};

class TestDataInputFactory : public DataInputFactory<TestData> {
public:
  TestDataInputFactory(absl::string_view factory_name, absl::string_view data)
      : factory_name_(std::string(factory_name)), value_(std::string(data)) {}

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
};

TEST(Matcher, TestMatcher) {
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
                  name: match
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
  Registry::InjectFactory<DataInputFactory<TestData>> inject_outer(outer_factory);

  auto inner_factory = TestDataInputFactory("inner_input", "foo");
  Registry::InjectFactory<DataInputFactory<TestData>> inject_inner(inner_factory);
  auto match_tree = factory.create(matcher);

  const auto result = match_tree->match(TestData());
  EXPECT_TRUE(result.match_completed_);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_TRUE(result.on_match_->action_.has_value());
}

TEST(Matcher, TestRecursiveMatcher) {
  const std::string yaml = R"EOF(
matcher_list:
  matchers:
  - on_match:
      matcher:
        matcher_list:
          matchers:
          - on_match:
              action:
                name: match
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
  Registry::InjectFactory<DataInputFactory<TestData>> inject_outer(outer_factory);

  auto inner_factory = TestDataInputFactory("inner_input", "foo");
  Registry::InjectFactory<DataInputFactory<TestData>> inject_inner(inner_factory);
  auto match_tree = factory.create(matcher);

  const auto result = match_tree->match(TestData());
  EXPECT_TRUE(result.match_completed_);
  EXPECT_TRUE(result.on_match_.has_value());
  EXPECT_FALSE(result.on_match_->action_.has_value());

  const auto recursive_result = evaluateMatch(*match_tree, TestData());
  EXPECT_TRUE(recursive_result.final_);
  EXPECT_TRUE(recursive_result.result_.has_value());
}
} // namespace Matcher
} // namespace Envoy