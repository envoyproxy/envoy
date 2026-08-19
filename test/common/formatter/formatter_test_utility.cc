#include "test/common/formatter/formatter_test_utility.h"

#include "source/common/formatter/serializer.h"
#include "source/common/json/json_utility.h"

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Formatter {
namespace {

// Pre-seeded into the sink so that a provider which clears instead of appends is caught.
constexpr absl::string_view SinkSentinel = "sentinel:";

// Asserts that a formatTo() run agrees with the format() result it is compared against.
void checkFormatTo(const std::optional<std::string>& expected, bool has_value,
                   const std::string& sink) {
  EXPECT_EQ(has_value, expected.has_value())
      << "formatTo() and format() disagree on whether a value was extracted";
  ASSERT_TRUE(absl::StartsWith(sink, SinkSentinel)) << "formatTo() must append, not overwrite";

  const absl::string_view appended = absl::string_view(sink).substr(SinkSentinel.size());
  if (has_value && expected.has_value()) {
    EXPECT_EQ(appended, *expected) << "formatTo() and format() produced different text";
  } else {
    EXPECT_EQ(appended, "") << "formatTo() wrote to the sink without reporting a value";
  }
}

// Asserts that a formatValueTo() run agrees with the formatValue() result it is compared against.
void checkFormatValueTo(const Protobuf::Value& expected, bool consumed,
                        const std::string& actual_json) {
  // A null or unset value is how formatValue() reports "no value"; formatValueTo() reports the
  // same thing by leaving the sink untouched.
  const bool expect_value = expected.kind_case() != Protobuf::Value::kNullValue &&
                            expected.kind_case() != Protobuf::Value::KIND_NOT_SET;
  EXPECT_EQ(consumed, expect_value)
      << "formatValueTo() and formatValue() disagree on whether a value was extracted";
  if (!expect_value) {
    EXPECT_EQ(actual_json, "") << "formatValueTo() wrote to the sink without reporting a value";
    return;
  }

  if (expected.kind_case() == Protobuf::Value::kNumberValue) {
    // Numbers are compared by value, not by text. The sink renders an integer exactly
    // ("1000000") while formatValue() carries a double that serializes to the shortest
    // round-trip form ("1e+06"); that difference is intended.
    double actual_number = 0;
    ASSERT_TRUE(absl::SimpleAtod(actual_json, &actual_number))
        << "formatValueTo() produced '" << actual_json << "' where a number was expected";
    EXPECT_DOUBLE_EQ(actual_number, expected.number_value())
        << "formatValueTo() and formatValue() produced different numbers";
    return;
  }

  std::string expected_json;
  Json::Utility::appendValueToString(expected, expected_json);
  EXPECT_EQ(actual_json, expected_json)
      << "formatValueTo() and formatValue() produced different JSON";
}

} // namespace

std::optional<std::string> formatForTest(const FormatterProvider& provider, const Context& context,
                                         const StreamInfo::StreamInfo& stream_info) {
  const std::optional<std::string> expected = provider.format(context, stream_info);
  std::string sink{SinkSentinel};
  const bool has_value = provider.formatTo(sink, context, stream_info);
  checkFormatTo(expected, has_value, sink);
  return expected;
}

Protobuf::Value formatValueForTest(const FormatterProvider& provider, const Context& context,
                                   const StreamInfo::StreamInfo& stream_info) {
  const Protobuf::Value expected = provider.formatValue(context, stream_info);
  std::string actual;
  JsonStringSerializer serializer(actual);
  ValueSink sink(serializer);
  provider.formatValueTo(sink, context, stream_info);
  checkFormatValueTo(expected, sink.consumed(), actual);
  return expected;
}

std::optional<std::string> formatForTest(const FormatterProvider& provider,
                                         const StreamInfo::StreamInfo& stream_info) {
  Envoy::Formatter::Context context;
  return formatForTest(provider, context, stream_info);
}

Protobuf::Value formatValueForTest(const FormatterProvider& provider,
                                   const StreamInfo::StreamInfo& stream_info) {
  Envoy::Formatter::Context context;
  return formatValueForTest(provider, context, stream_info);
}

} // namespace Formatter
} // namespace Envoy
