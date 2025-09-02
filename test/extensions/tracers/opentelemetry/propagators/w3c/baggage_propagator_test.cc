#include "source/extensions/tracers/opentelemetry/propagators/w3c/baggage_propagator.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

using testing::HasSubstr;

class BaggagePropagatorTest : public testing::Test {
public:
  BaggagePropagatorTest() : propagator_(std::make_unique<BaggagePropagator>()) {}

protected:
  std::unique_ptr<BaggagePropagator> propagator_;
};

TEST_F(BaggagePropagatorTest, ExtractAlwaysFails) {
  // Baggage propagator doesn't extract trace context, only validates baggage data
  Tracing::TestTraceContextImpl trace_context{{"baggage", "key1=value1,key2=value2"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              HasSubstr("Baggage propagator cannot extract span context"));
}

TEST_F(BaggagePropagatorTest, ExtractFailsWithoutBaggageHeader) {
  Tracing::TestTraceContextImpl trace_context{{"other-header", "value"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              HasSubstr("Baggage propagator cannot extract span context"));
}

TEST_F(BaggagePropagatorTest, ExtractWithValidBaggageFormat) {
  // Even with valid baggage, extract should fail for span context but validate baggage
  Tracing::TestTraceContextImpl trace_context{{"baggage", "userId=alice,serverNode=DF:28"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              HasSubstr("Baggage propagator cannot extract span context"));
}

TEST_F(BaggagePropagatorTest, ExtractWithInvalidBaggageFormat) {
  // Invalid baggage format should still fail extract but not crash
  Tracing::TestTraceContextImpl trace_context{{"baggage", "invalid=,=value,malformed"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              HasSubstr("Baggage propagator cannot extract span context"));
}

TEST_F(BaggagePropagatorTest, ExtractWithOversizedBaggage) {
  // Create an oversized baggage header (> 8KB)
  std::string large_value(9000, 'x');
  std::string baggage_header = "key=" + large_value;
  Tracing::TestTraceContextImpl trace_context{{"baggage", baggage_header}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              HasSubstr("Baggage propagator cannot extract span context"));
}

TEST_F(BaggagePropagatorTest, InjectDoesNothingCurrently) {
  // Baggage propagator doesn't inject trace context currently due to SpanContext limitations
  SpanContext span_context("00", "00000000000000000000000000000001", "0000000000000002", true, "");
  Tracing::TestTraceContextImpl trace_context{};

  propagator_->inject(span_context, trace_context);

  // Should not inject any headers currently
  EXPECT_TRUE(!trace_context.get("baggage") || trace_context.get("baggage")->empty());
  EXPECT_TRUE(!trace_context.get("traceparent") || trace_context.get("traceparent")->empty());
}

TEST_F(BaggagePropagatorTest, InjectPreservesExistingHeaders) {
  // Ensure inject doesn't remove existing headers
  SpanContext span_context("00", "00000000000000000000000000000001", "0000000000000002", true, "");
  Tracing::TestTraceContextImpl trace_context{{"existing-header", "existing-value"}};

  propagator_->inject(span_context, trace_context);

  // Should preserve existing headers
  EXPECT_EQ(trace_context.get("existing-header"), "existing-value");
  EXPECT_TRUE(!trace_context.get("baggage") || trace_context.get("baggage")->empty());
}

TEST_F(BaggagePropagatorTest, FieldsReturnsBaggageHeader) {
  auto fields = propagator_->fields();

  EXPECT_EQ(fields.size(), 1);
  EXPECT_THAT(fields, testing::Contains("baggage"));
}

TEST_F(BaggagePropagatorTest, NameReturnsBaggage) { EXPECT_EQ(propagator_->name(), "baggage"); }

TEST_F(BaggagePropagatorTest, ParseValidBaggageEntries) {
  // Test baggage parsing with valid entries (internal method testing via extract)
  Tracing::TestTraceContextImpl trace_context{
      {"baggage", "userId=alice,serverNode=DF28,isProduction=true"}};

  // Extract validates the baggage format internally
  auto result = propagator_->extract(trace_context);

  // Should still fail for span context but not crash during baggage parsing
  EXPECT_FALSE(result.ok());
}

TEST_F(BaggagePropagatorTest, ParseBaggageWithProperties) {
  // Test baggage with properties (should ignore properties for now)
  Tracing::TestTraceContextImpl trace_context{
      {"baggage", "userId=alice;property=value,serverNode=DF28"}};

  auto result = propagator_->extract(trace_context);

  // Should still fail for span context but handle properties gracefully
  EXPECT_FALSE(result.ok());
}

TEST_F(BaggagePropagatorTest, ParseBaggageWithWhitespace) {
  // Test baggage with various whitespace scenarios
  Tracing::TestTraceContextImpl trace_context{{"baggage", " userId = alice , serverNode = DF28 "}};

  auto result = propagator_->extract(trace_context);

  // Should still fail for span context but handle whitespace gracefully
  EXPECT_FALSE(result.ok());
}

TEST_F(BaggagePropagatorTest, ParseEmptyBaggageEntries) {
  // Test various empty/invalid entry scenarios
  Tracing::TestTraceContextImpl trace_context{{"baggage", ",,,userId=alice,,,"}};

  auto result = propagator_->extract(trace_context);

  // Should still fail for span context but handle empty entries gracefully
  EXPECT_FALSE(result.ok());
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
