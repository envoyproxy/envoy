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
  MatchingDataType get() override { return data_; }

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

struct TestFloatInput : public DataInput<TestData> {
  explicit TestFloatInput(DataInputGetResult result) : result_(result) {}
  DataInputGetResult get(const TestData&) const override { return result_; }
  absl::string_view dataInputType() const override { return "float"; }
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
    // Note, here is using `TestInput` same as `TestDataInputStringFactory`.
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

class TestDataInputFloatFactory : public DataInputFactory<TestData> {
public:
  TestDataInputFloatFactory(DataInputGetResult result) : result_(result), injection_(*this) {}
  TestDataInputFloatFactory(float)
      : TestDataInputFloatFactory(
            {DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()}) {}
  DataInputFactoryCb<TestData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return [&]() { return std::make_unique<TestFloatInput>(result_); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::FloatValue>();
  }
  std::string name() const override { return "float"; }

private:
  const DataInputGetResult result_;
  Registry::InjectFactory<DataInputFactory<TestData>> injection_;
};

// A matcher that evaluates to the configured value.
// Note, `BoolMatcher` supports string type data input only as `TestDataInputBoolFactory` is using
// `TestInput` same as `TestDataInputStringFactory`.
struct BoolMatcher : public InputMatcher {
  explicit BoolMatcher(bool value) : value_(value) {}

  bool match(const MatchingDataType&) override { return value_; }
  const bool value_;
};

// An InputMatcher that evaluates the input against a provided callback.
struct TestMatcher : public InputMatcher {
  explicit TestMatcher(std::function<bool(absl::optional<absl::string_view>)> predicate)
      : predicate_(predicate) {}

  bool match(const MatchingDataType& input) override {
    if (absl::holds_alternative<absl::monostate>(input)) {
      return false;
    }
    return predicate_(absl::get<std::string>(input));
  }

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
  bool match(const MatchingDataType&) override { return false; }
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
  bool match(const MatchingDataType& input) override {
    if (absl::holds_alternative<absl::monostate>(input)) {
      return false;
    }

    return str_value_ == absl::get<std::string>(input);
  }

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
  MatchingDataType data =
      input.has_value() ? MatchingDataType(std::string(*input)) : absl::monostate();

  return SingleFieldMatcher<TestData>::create(
             std::make_unique<TestInput>(DataInputGetResult{availability, std::move(data)}),
             std::make_unique<TestMatcher>(predicate))
      .value();
}

// Creates a StringAction from a provided string.
std::unique_ptr<StringAction> stringValue(absl::string_view value) {
  return std::make_unique<StringAction>(std::string(value));
}

// Creates an OnMatch that evaluates to a StringValue with the provided value.
template <class T> OnMatch<T> stringOnMatch(absl::string_view value, bool keep_matching = false) {
  return OnMatch<T>{[s = std::string(value)]() { return stringValue(s); }, nullptr, keep_matching};
}

inline void PrintTo(const Action& action, std::ostream* os) {
  if (action.typeUrl() == "google.protobuf.StringValue") {
    *os << "{string_value=\"" << action.getTyped<StringAction>().string_ << "\"}";
    return;
  }
  *os << "{type=" << action.typeUrl() << "}";
}

inline void PrintTo(const ActionFactoryCb& action_cb, std::ostream* os) {
  if (action_cb == nullptr) {
    *os << "nullptr";
    return;
  }
  ActionPtr action = action_cb();
  PrintTo(*action, os);
}

inline void PrintTo(const MatchState state, std::ostream* os) {
  switch (state) {
  case MatchState::UnableToMatch:
    *os << "UnableToMatch";
    break;
  case MatchState::MatchComplete:
    *os << "MatchComplete";
    break;
  }
}

inline void PrintTo(const MatchTree<TestData>& matcher, std::ostream* os) {
  *os << "{type=" << typeid(matcher).name() << "}";
}

inline void PrintTo(const OnMatch<TestData>& on_match, std::ostream* os) {
  if (on_match.action_cb_) {
    *os << "{action_cb_=";
    PrintTo(on_match.action_cb_, os);
    *os << "}";
  } else if (on_match.matcher_) {
    *os << "{matcher_=";
    PrintTo(*on_match.matcher_, os);
    *os << "}";
  } else {
    *os << "{invalid, no value set}";
  }
}

inline void PrintTo(const MatchTree<TestData>::MatchResult& result, std::ostream* os) {
  *os << "{match_state_=";
  PrintTo(result.match_state_, os);
  *os << ", on_match_=";
  if (result.on_match_.has_value()) {
    PrintTo(result.on_match_.value(), os);
  } else {
    *os << "nullopt";
  }
  *os << "}";
}

MATCHER(HasNotEnoughData, "") {
  // Takes a MatchTree<TestData>::MatchResult& and validates that it
  // is in the UnableToMatch state.
  return arg.match_state_ == MatchState::UnableToMatch && !arg.on_match_.has_value();
}

MATCHER_P(IsStringAction, matcher, "") {
  // Takes an ActionFactoryCb argument, and compares its StringAction's string against matcher.
  if (arg == nullptr) {
    return false;
  }
  ActionPtr action = arg();
  if (action->typeUrl() != "google.protobuf.StringValue") {
    return false;
  }
  return ::testing::ExplainMatchResult(testing::Matcher<std::string>(matcher),
                                       action->template getTyped<StringAction>().string_,
                                       result_listener);
}

MATCHER_P(HasStringAction, matcher, "") {
  // Takes a MatchTree<TestData>::MatchResult& and validates that it
  // is a StringAction with contents matching matcher.
  if (arg.match_state_ != MatchState::MatchComplete || !arg.on_match_.has_value() ||
      arg.on_match_->matcher_ != nullptr) {
    return false;
  }
  return ::testing::ExplainMatchResult(IsStringAction(matcher), arg.on_match_->action_cb_,
                                       result_listener);
}

MATCHER(HasNoMatch, "") {
  // Takes a MatchTree<TestData>::MatchResult& and validates that it
  // is MatchComplete with no on_match_.
  return arg.match_state_ == MatchState::MatchComplete && !arg.on_match_.has_value();
}

MATCHER(HasSubMatcher, "") {
  // Takes a MatchTree<TestData>::MatchResult& and validates that it
  // has a matcher_ value in on_match_.
  return arg.match_state_ == MatchState::MatchComplete && arg.on_match_.has_value() &&
         arg.on_match_->matcher_ != nullptr;
}

MATCHER_P(HasResult, m, "") {
  // Accepts a MaybeMatchResult argument.
  if (arg.match_state_ != MatchState::MatchComplete) {
    *result_listener << "match_state_ is not MatchComplete";
    return false;
  }
  if (arg.result_ == nullptr) {
    *result_listener << "result_ is null";
    return false;
  }
  return ExplainMatchResult(m, arg.result_, result_listener);
}

MATCHER(HasNoMatchResult, "") {
  // Accepts a MaybeMatchResult argument.
  if (arg.match_state_ != MatchState::MatchComplete) {
    *result_listener << "match_state_ is not MatchComplete";
    return false;
  }
  if (arg.result_ != nullptr) {
    *result_listener << "result_ is not null";
    return false;
  }
  return true;
}

MATCHER(HasFailureResult, "") {
  // Accepts a MaybeMatchResult argument.
  if (arg.match_state_ != MatchState::UnableToMatch) {
    *result_listener << "match_state_ is not UnableToMatch";
    return false;
  }
  if (arg.result_ != nullptr) {
    *result_listener << "result_ is not null";
    return false;
  }
  return true;
}

} // namespace Matcher
} // namespace Envoy
