#include "source/extensions/propagators/w3c/baggage_propagator.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {

using testing::HasSubstr;

class BaggagePropagatorTest : public testing::Test {
public:
  BaggagePropagatorTest() : propagator_(std::make_unique<BaggagePropagator>()) {}

protected:
  std::unique_ptr<BaggagePropagator> propagator_;
};

TEST_F(BaggagePropagatorTest, ExtractDoesNotProvideTraceContext) {
  Tracing::TestTraceContextImpl trace_context{{"baggage", "key1=value1,key2=value2"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              HasSubstr("Baggage propagator does not extract trace context"));
}

TEST_F(BaggagePropagatorTest, InjectPreservesExistingBaggage) {
  Tracers::OpenTelemetry::SpanContext span_context("00", "0af7651916cd43dd8448eb211c80319c",
                                                   "b7ad6b7169203331", true, "");
  Tracing::TestTraceContextImpl trace_context{{"baggage", "existing=value"}};

  propagator_->inject(span_context, trace_context);

  auto baggage = trace_context.get("baggage");
  EXPECT_TRUE(baggage.has_value());
  EXPECT_EQ("existing=value", baggage.value());
}

TEST_F(BaggagePropagatorTest, ParseBaggageValid) {
  std::string baggage_header = "key1=value1,key2=value2";

  auto result = propagator_->parseBaggage(baggage_header);

  EXPECT_EQ(2, result.size());
  EXPECT_EQ("value1", result["key1"]);
  EXPECT_EQ("value2", result["key2"]);
}

TEST_F(BaggagePropagatorTest, ParseBaggageWithSpaces) {
  std::string baggage_header = " key1 = value1 , key2 = value2 ";

  auto result = propagator_->parseBaggage(baggage_header);

  EXPECT_EQ(2, result.size());
  EXPECT_EQ("value1", result["key1"]);
  EXPECT_EQ("value2", result["key2"]);
}

TEST_F(BaggagePropagatorTest, ParseBaggageEmpty) {
  std::string baggage_header = "";

  auto result = propagator_->parseBaggage(baggage_header);

  EXPECT_EQ(0, result.size());
}

TEST_F(BaggagePropagatorTest, ParseBaggageInvalidFormat) {
  std::string baggage_header = "invalid-format";

  auto result = propagator_->parseBaggage(baggage_header);

  EXPECT_EQ(0, result.size());
}

TEST_F(BaggagePropagatorTest, FormatBaggage) {
  absl::flat_hash_map<std::string, std::string> baggage_entries = {{"key1", "value1"},
                                                                   {"key2", "value2"}};

  auto result = propagator_->formatBaggage(baggage_entries);

  // Order might vary, so check for presence of both entries
  EXPECT_THAT(result, testing::AnyOf(testing::Eq("key1=value1,key2=value2"),
                                     testing::Eq("key2=value2,key1=value1")));
}

TEST_F(BaggagePropagatorTest, IsValidBaggageKey) {
  EXPECT_TRUE(propagator_->isValidBaggageKey("valid_key"));
  EXPECT_TRUE(propagator_->isValidBaggageKey("key123"));
  EXPECT_FALSE(propagator_->isValidBaggageKey(""));

  // Key too long (over 256 characters)
  std::string long_key(257, 'a');
  EXPECT_FALSE(propagator_->isValidBaggageKey(long_key));
}

TEST_F(BaggagePropagatorTest, IsValidBaggageValue) {
  EXPECT_TRUE(propagator_->isValidBaggageValue("valid_value"));
  EXPECT_TRUE(propagator_->isValidBaggageValue(""));

  // Value too long (over 4096 characters)
  std::string long_value(4097, 'a');
  EXPECT_FALSE(propagator_->isValidBaggageValue(long_value));
}

TEST_F(BaggagePropagatorTest, Fields) {
  auto fields = propagator_->fields();

  EXPECT_EQ(1, fields.size());
  EXPECT_EQ("baggage", fields[0]);
}

TEST_F(BaggagePropagatorTest, Name) { EXPECT_EQ("baggage", propagator_->name()); }

} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
