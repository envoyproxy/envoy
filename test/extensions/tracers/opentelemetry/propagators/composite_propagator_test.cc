#include "source/extensions/tracers/opentelemetry/propagators/propagator.h"
#include "source/extensions/tracers/opentelemetry/propagators/b3/b3_propagator.h"
#include "source/extensions/tracers/opentelemetry/propagators/w3c/baggage_propagator.h"
#include "source/extensions/tracers/opentelemetry/propagators/w3c/w3c_trace_context_propagator.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

class CompositePropagatorTest : public testing::Test {
public:
  CompositePropagatorTest() {
    // Create a composite propagator with W3C, B3, and Baggage propagators
    std::vector<TextMapPropagatorPtr> propagators;
    propagators.push_back(std::make_unique<W3CTraceContextPropagator>());
    propagators.push_back(std::make_unique<B3Propagator>());
    propagators.push_back(std::make_unique<BaggagePropagator>());

    composite_propagator_ = std::make_unique<CompositePropagator>(std::move(propagators));
  }

protected:
  std::unique_ptr<CompositePropagator> composite_propagator_;
};

TEST_F(CompositePropagatorTest, ExtractWithW3CHeaders) {
  Tracing::TestTraceContextImpl trace_context{
      {"traceparent", "00-00000000000000000000000000000001-0000000000000002-01"}};

  auto result = composite_propagator_->extract(trace_context);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->traceId(), "00000000000000000000000000000001");
  EXPECT_EQ(result->spanId(), "0000000000000002");
  EXPECT_TRUE(result->sampled());
}

TEST_F(CompositePropagatorTest, ExtractWithB3Headers) {
  Tracing::TestTraceContextImpl trace_context{{"X-B3-TraceId", "0000000000000001"},
                                              {"X-B3-SpanId", "0000000000000002"},
                                              {"X-B3-Sampled", "1"}};

  auto result = composite_propagator_->extract(trace_context);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->traceId(), "0000000000000001");
  EXPECT_EQ(result->spanId(), "0000000000000002");
  EXPECT_TRUE(result->sampled());
}

TEST_F(CompositePropagatorTest, ExtractUsesFirstSuccessfulPropagator) {
  // Both W3C and B3 headers present - should use W3C (first in order)
  Tracing::TestTraceContextImpl trace_context{
      {"traceparent", "00-00000000000000000000000000000001-0000000000000002-01"},
      {"X-B3-TraceId", "0000000000000003"},
      {"X-B3-SpanId", "0000000000000004"},
      {"X-B3-Sampled", "1"}};

  auto result = composite_propagator_->extract(trace_context);

  EXPECT_TRUE(result.ok());
  // Should use W3C values, not B3
  EXPECT_EQ(result->traceId(), "00000000000000000000000000000001");
  EXPECT_EQ(result->spanId(), "0000000000000002");
}

TEST_F(CompositePropagatorTest, ExtractFailsWithNoValidHeaders) {
  Tracing::TestTraceContextImpl trace_context{{"other-header", "value"}};

  auto result = composite_propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("No propagator could extract span context"));
}

TEST_F(CompositePropagatorTest, InjectUsesAllPropagatorsExceptBaggage) {
  SpanContext span_context("00", "00000000000000000000000000000001", "0000000000000002", true,
                           "vendor=state");
  Tracing::TestTraceContextImpl trace_context{};

  composite_propagator_->inject(span_context, trace_context);

  // Should inject W3C headers
  EXPECT_EQ(trace_context.get("traceparent"),
            "00-00000000000000000000000000000001-0000000000000002-01");
  EXPECT_EQ(trace_context.get("tracestate"), "vendor=state");

  // Should inject B3 headers
  EXPECT_EQ(trace_context.get("b3"), "00000000000000000000000000000001-0000000000000002-1");
  EXPECT_EQ(trace_context.get("X-B3-TraceId"), "00000000000000000000000000000001");
  EXPECT_EQ(trace_context.get("X-B3-SpanId"), "0000000000000002");
  EXPECT_EQ(trace_context.get("X-B3-Sampled"), "1");

  // Should NOT inject baggage headers (baggage propagator is skipped for injection)
  EXPECT_TRUE(!trace_context.get("baggage") || trace_context.get("baggage")->empty());
}

TEST_F(CompositePropagatorTest, PropagationHeaderPresentWithW3C) {
  Tracing::TestTraceContextImpl trace_context{
      {"traceparent", "00-00000000000000000000000000000001-0000000000000002-01"}};

  EXPECT_TRUE(composite_propagator_->propagationHeaderPresent(trace_context));
}

TEST_F(CompositePropagatorTest, PropagationHeaderPresentWithB3) {
  Tracing::TestTraceContextImpl trace_context{{"X-B3-TraceId", "0000000000000001"},
                                              {"X-B3-SpanId", "0000000000000002"},
                                              {"X-B3-Sampled", "1"}};

  EXPECT_TRUE(composite_propagator_->propagationHeaderPresent(trace_context));
}

TEST_F(CompositePropagatorTest, PropagationHeaderNotPresentWithBaggageOnly) {
  // Baggage header doesn't count as propagation header for trace context
  Tracing::TestTraceContextImpl trace_context{{"baggage", "key=value"}};

  EXPECT_FALSE(composite_propagator_->propagationHeaderPresent(trace_context));
}

TEST_F(CompositePropagatorTest, PropagationHeaderNotPresentWithNoHeaders) {
  Tracing::TestTraceContextImpl trace_context{};

  EXPECT_FALSE(composite_propagator_->propagationHeaderPresent(trace_context));
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
