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
  ActionConstSharedPtr createAction(const Protobuf::Message& config, absl::string_view&,
                                    ProtobufMessage::ValidationVisitor&) override {
    const auto& string = dynamic_cast<const ProtobufWkt::StringValue&>(config);
    return std::make_shared<StringAction>(string.value());
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

void PrintTo(const FieldMatchResult& result, std::ostream* os) {
  if (result.isInsufficientData()) {
    *os << "InsufficientData";
  } else if (result.isNoMatch()) {
    *os << "NoMatch";
  } else if (result.isMatched()) {
    *os << "Matched";
  } else {
    *os << "UnknownState";
  }
}

// Creates an OnMatch that evaluates to a StringValue with the provided value.
template <class T> OnMatch<T> stringOnMatch(absl::string_view value, bool keep_matching = false) {
  return OnMatch<T>{std::make_shared<StringAction>(std::string(value)), nullptr, keep_matching};
}

inline void PrintTo(const Action& action, std::ostream* os) {
  if (action.typeUrl() == "google.protobuf.StringValue") {
    *os << "{string_value=\"" << action.getTyped<StringAction>().string_ << "\"}";
    return;
  }
  *os << "{type=" << action.typeUrl() << "}";
}

inline void PrintTo(const MatchResult& result, std::ostream* os) {
  if (result.isInsufficientData()) {
    *os << "InsufficientData";
  } else if (result.isNoMatch()) {
    *os << "NoMatch";
  } else if (result.isMatch()) {
    *os << "Match{Action=";
    PrintTo(*result.action(), os);
    *os << "}";
  } else {
    *os << "UnknownState";
  }
}

inline void PrintTo(const MatchTree<TestData>& matcher, std::ostream* os) {
  *os << "{type=" << typeid(matcher).name() << "}";
}

inline void PrintTo(const OnMatch<TestData>& on_match, std::ostream* os) {
  if (on_match.action_) {
    *os << "{action_=";
    PrintTo(on_match.action_, os);
    *os << "}";
  } else if (on_match.matcher_) {
    *os << "{matcher_=";
    PrintTo(*on_match.matcher_, os);
    *os << "}";
  } else {
    *os << "{invalid, no value set}";
  }
}

MATCHER(HasInsufficientData, "") {
  // Takes a MatchResult& and validates that it
  // is in the InsufficientData state.
  return arg.isInsufficientData();
}

MATCHER_P(IsActionWithType, matcher, "") {
  // Takes an ActionConstSharedPtr argument, and compares its action type against matcher.
  if (arg == nullptr) {
    return false;
  }
  return ::testing::ExplainMatchResult(testing::Matcher<absl::string_view>(matcher), arg->typeUrl(),
                                       result_listener);
}

MATCHER_P(IsStringAction, matcher, "") {
  // Takes an ActionConstSharedPtr argument, and compares its StringAction's string against matcher.
  if (arg == nullptr) {
    return false;
  }

  if (arg->typeUrl() != "google.protobuf.StringValue") {
    return false;
  }
  return ::testing::ExplainMatchResult(testing::Matcher<std::string>(matcher),
                                       arg->template getTyped<StringAction>().string_,
                                       result_listener);
}

MATCHER_P(HasStringAction, matcher, "") {
  // Takes a MatchResult& and validates that it
  // has a StringAction with contents matching matcher.
  if (!arg.isMatch()) {
    return false;
  }
  return ::testing::ExplainMatchResult(IsStringAction(matcher), arg.action(), result_listener);
}

MATCHER_P(HasActionWithType, matcher, "") {
  // Takes a MatchResult& and validates that it
  // has an action whose type matches matcher.
  if (!arg.isMatch()) {
    return false;
  }
  return ::testing::ExplainMatchResult(IsActionWithType(matcher), arg.action(), result_listener);
}

MATCHER(HasNoMatch, "") {
  // Takes a MatchResult& and validates that it is NoMatch.
  return arg.isNoMatch();
}

} // namespace Matcher
} // namespace Envoy
