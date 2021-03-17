#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/protobuf/message_validator.h"

#include "common/matcher/matcher.h"

#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Matcher {

// Empty structure used as the input data for the test inputs.
struct TestData {
  static absl::string_view name() { return "test"; }
};

// A DataInput that returns the configured value every time.
struct TestInput : public DataInput<TestData> {
  explicit TestInput(DataInputGetResult result) : result_(result) {}
  DataInputGetResult get(const TestData&) override { return result_; }

  DataInputGetResult result_;
};

// Self-injecting factory for TestInput.
class TestDataInputFactory : public DataInputFactory<TestData> {
public:
  TestDataInputFactory(absl::string_view factory_name, absl::string_view data)
      : factory_name_(std::string(factory_name)), value_(std::string(data)), injection_(*this) {}

  DataInputPtr<TestData> createDataInput(const Protobuf::Message&,
                                         Server::Configuration::FactoryContext&) override {
    return std::make_unique<TestInput>(
        DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable, value_});
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
class StringActionFactory : public ActionFactory {
public:
  ActionFactoryCb createActionFactoryCb(const Protobuf::Message& config,
                                        Server::Configuration::FactoryContext&) override {
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

  InputMatcherPtr createInputMatcher(const Protobuf::Message&,
                                     Server::Configuration::FactoryContext&) override {
    return std::make_unique<NeverMatch>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }

  std::string name() const override { return "never_match"; }

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
      std::make_unique<TestInput>(DataInputGetResult{availability, input}),
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

} // namespace Matcher
} // namespace Envoy