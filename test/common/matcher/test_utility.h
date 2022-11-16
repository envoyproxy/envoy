#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/protobuf/message_validator.h"

#include "source/common/matcher/matcher.h"

#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Matcher {

// Empty structure used as the input data for the test inputs.
struct TestData {
  static absl::string_view name() { return "test"; }
};

// A CommonProtocolInput that returns the configured value every time.
struct CommonProtocolTestInput : public CommonProtocolInput {
  explicit CommonProtocolTestInput(const std::string& data) : data_(data) {}
  absl::optional<std::string> get() override { return data_; }

  const std::string data_;
};
class TestCommonProtocolInputFactory : public CommonProtocolInputFactory {
public:
  TestCommonProtocolInputFactory(absl::string_view factory_name, absl::string_view data)
      : factory_name_(std::string(factory_name)), value_(std::string(data)), injection_(*this) {}

  CommonProtocolInputFactoryCb
  createCommonProtocolInputFactoryCb(const Protobuf::Message&,
                                     ProtobufMessage::ValidationVisitor&) override {
    return [&]() { return std::make_unique<CommonProtocolTestInput>(value_); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }
  std::string name() const override { return factory_name_; }

private:
  const std::string factory_name_;
  const std::string value_;
  Registry::InjectFactory<CommonProtocolInputFactory> injection_;
};

// A DataInput that returns the configured value every time.
struct TestInput : public DataInput<TestData> {
  explicit TestInput(DataInputGetResult result) : result_(result) {}
  DataInputGetResult get(const TestData&) const override { return result_; }

  DataInputGetResult result_;
};

// Self-injecting factory for TestInput.
class TestDataInputStringFactory : public DataInputFactory<TestData> {
public:
  TestDataInputStringFactory(DataInputGetResult result) : result_(result), injection_(*this) {}
  TestDataInputStringFactory(absl::string_view data)
      : TestDataInputStringFactory(
            {DataInputGetResult::DataAvailability::AllDataAvailable, std::string(data)}) {}
  DataInputFactoryCb<TestData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return [&]() { return std::make_unique<TestInput>(result_); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }
  std::string name() const override { return "string"; }

private:
  const DataInputGetResult result_;
  Registry::InjectFactory<DataInputFactory<TestData>> injection_;
};

// Secondary data input to avoid duplicate type registration.
class TestDataInputBoolFactory : public DataInputFactory<TestData> {
public:
  TestDataInputBoolFactory(DataInputGetResult result) : result_(result), injection_(*this) {}
  TestDataInputBoolFactory(absl::string_view data)
      : TestDataInputBoolFactory(
            {DataInputGetResult::DataAvailability::AllDataAvailable, std::string(data)}) {}
  DataInputFactoryCb<TestData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return [&]() { return std::make_unique<TestInput>(result_); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::BoolValue>();
  }
  std::string name() const override { return "bool"; }

private:
  const DataInputGetResult result_;
  Registry::InjectFactory<DataInputFactory<TestData>> injection_;
};

// A matcher that evaluates to the configured value.
struct BoolMatcher : public InputMatcher {
  explicit BoolMatcher(bool value) : value_(value) {}

  bool match(absl::optional<absl::string_view>) override { return value_; }

  const bool value_;
};

// An InputMatcher that evaluates the input against a provided callback.
struct TestMatcher : public InputMatcher {
  explicit TestMatcher(std::function<bool(absl::optional<absl::string_view>)> predicate)
      : predicate_(predicate) {}

  bool match(absl::optional<absl::string_view> input) override { return predicate_(input); }

  std::function<bool(absl::optional<absl::string_view>)> predicate_;
};

// An action that evaluates to a proto StringValue.
struct StringAction : public ActionBase<ProtobufWkt::StringValue> {
  explicit StringAction(const std::string& string) : string_(string) {}

  const std::string string_;

  bool operator==(const StringAction& other) const { return string_ == other.string_; }
};

// Factory for StringAction.
class StringActionFactory : public ActionFactory<absl::string_view> {
public:
  ActionFactoryCb createActionFactoryCb(const Protobuf::Message& config, absl::string_view&,
                                        ProtobufMessage::ValidationVisitor&) override {
    const auto& string = dynamic_cast<const ProtobufWkt::StringValue&>(config);
    return [string]() { return std::make_unique<StringAction>(string.value()); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }
  std::string name() const override { return "string_action"; }
};

// An InputMatcher that always returns false.
class NeverMatch : public InputMatcher {
public:
  bool match(absl::optional<absl::string_view>) override { return false; }
};

/**
 * A self-injecting factory for the NeverMatch InputMatcher.
 */
class NeverMatchFactory : public InputMatcherFactory {
public:
  NeverMatchFactory() : inject_factory_(*this) {}

  InputMatcherFactoryCb
  createInputMatcherFactoryCb(const Protobuf::Message&,
                              Server::Configuration::ServerFactoryContext&) override {
    return []() { return std::make_unique<NeverMatch>(); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }

  std::string name() const override { return "never_match"; }

  Registry::InjectFactory<InputMatcherFactory> inject_factory_;
};

// Custom matcher to perform string comparison.
class CustomStringMatcher : public InputMatcher {
public:
  explicit CustomStringMatcher(const std::string& str) : str_value_(str) {}
  bool match(absl::optional<absl::string_view> str) override { return str_value_ == str; }

private:
  std::string str_value_;
};

/**
 * A self-injecting factory for the CustomStringMatcher InputMatcher.
 */
class CustomStringMatcherFactory : public InputMatcherFactory {
public:
  CustomStringMatcherFactory() : inject_factory_(*this) {}

  InputMatcherFactoryCb
  createInputMatcherFactoryCb(const Protobuf::Message& config,
                              Server::Configuration::ServerFactoryContext&) override {
    const auto& string = dynamic_cast<const ProtobufWkt::StringValue&>(config);
    return [string]() { return std::make_unique<CustomStringMatcher>(string.value()); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }

  std::string name() const override { return "custom_match"; }

  Registry::InjectFactory<InputMatcherFactory> inject_factory_;
};

/**
 * Creates a SingleFieldMatcher for use in test.
 * @param input the optional input that should be provided to the SingleFieldMatcher
 * @param predicate the predicate to evaluate against the input
 * @param availability the data availability to use for the input.
 */
SingleFieldMatcherPtr<TestData>
createSingleMatcher(absl::optional<absl::string_view> input,
                    std::function<bool(absl::optional<absl::string_view>)> predicate,
                    DataInputGetResult::DataAvailability availability =
                        DataInputGetResult::DataAvailability::AllDataAvailable) {
  return std::make_unique<SingleFieldMatcher<TestData>>(
      std::make_unique<TestInput>(DataInputGetResult{
          availability, input ? absl::make_optional(std::string(*input)) : absl::nullopt}),
      std::make_unique<TestMatcher>(predicate));
}

// Creates a StringAction from a provided string.
std::unique_ptr<StringAction> stringValue(absl::string_view value) {
  return std::make_unique<StringAction>(std::string(value));
}

// Creates an OnMatch that evaluates to a StringValue with the provided value.
template <class T> OnMatch<T> stringOnMatch(absl::string_view value) {
  return OnMatch<T>{[s = std::string(value)]() { return stringValue(s); }, nullptr};
}

// Verifies the match tree completes the matching with an not match result.
void verifyNoMatch(const MatchTree<TestData>::MatchResult& result) {
  EXPECT_EQ(MatchState::MatchComplete, result.match_state_);
  EXPECT_FALSE(result.on_match_.has_value());
}

// Verifies the match tree completes the matching with the expected value.
void verifyImmediateMatch(const MatchTree<TestData>::MatchResult& result,
                          absl::string_view expected_value) {
  EXPECT_EQ(MatchState::MatchComplete, result.match_state_);
  EXPECT_TRUE(result.on_match_.has_value());

  EXPECT_EQ(nullptr, result.on_match_->matcher_);
  EXPECT_NE(result.on_match_->action_cb_, nullptr);

  EXPECT_EQ(result.on_match_->action_cb_().get()->getTyped<StringAction>(),
            *stringValue(expected_value));
}

// Verifies the match tree fails to match since the data are not enough.
void verifyNotEnoughDataForMatch(const MatchTree<TestData>::MatchResult& result) {
  EXPECT_EQ(MatchState::UnableToMatch, result.match_state_);
  EXPECT_FALSE(result.on_match_.has_value());
}

} // namespace Matcher
} // namespace Envoy
