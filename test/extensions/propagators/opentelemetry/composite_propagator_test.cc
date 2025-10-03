#include "source/extensions/propagators/opentelemetry/propagator.h"
#include "source/extensions/propagators/opentelemetry/b3/propagator.h"
#include "source/extensions/propagators/opentelemetry/w3c/baggage_propagator.h"
#include "source/extensions/propagators/opentelemetry/w3c/trace_context_propagator.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
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

TEST_F(CompositePropagatorTest, ExtractsPropagationHeaderPresent) {
  Tracing::TestTraceContextImpl trace_context{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"}};

  EXPECT_TRUE(composite_propagator_->propagationHeaderPresent(trace_context));
}

TEST_F(CompositePropagatorTest, ExtractsPropagationHeaderAbsent) {
  Tracing::TestTraceContextImpl trace_context{{}};

  EXPECT_FALSE(composite_propagator_->propagationHeaderPresent(trace_context));
}

TEST_F(CompositePropagatorTest, ExtractsW3CTraceparent) {
  Tracing::TestTraceContextImpl trace_context{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"}};

  auto result = composite_propagator_->extract(trace_context);
  ASSERT_TRUE(result.ok());

  auto span_context = result.value();
  EXPECT_EQ("0af7651916cd43dd8448eb211c80319c", span_context.traceId());
  EXPECT_EQ("b7ad6b7169203331", span_context.spanId());
  EXPECT_TRUE(span_context.sampled());
}

TEST_F(CompositePropagatorTest, ExtractsB3Headers) {
  Tracing::TestTraceContextImpl trace_context{{"X-B3-TraceId", "0af7651916cd43dd8448eb211c80319c"},
                                              {"X-B3-SpanId", "b7ad6b7169203331"},
                                              {"X-B3-Sampled", "1"}};

  auto result = composite_propagator_->extract(trace_context);
  ASSERT_TRUE(result.ok());

  auto span_context = result.value();
  EXPECT_EQ("0af7651916cd43dd8448eb211c80319c", span_context.traceId());
  EXPECT_EQ("b7ad6b7169203331", span_context.spanId());
  EXPECT_TRUE(span_context.sampled());
}

TEST_F(CompositePropagatorTest, InjectsAllFormats) {
  Tracers::OpenTelemetry::SpanContext span_context("00", "0af7651916cd43dd8448eb211c80319c",
                                                   "b7ad6b7169203331", true, "");
  Tracing::TestTraceContextImpl trace_context{{}};

  composite_propagator_->inject(span_context, trace_context);

  // Should have W3C traceparent
  auto traceparent = trace_context.get("traceparent");
  EXPECT_TRUE(traceparent.has_value());
  EXPECT_EQ("00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01", traceparent.value());

  // Should have B3 headers
  auto b3_trace_id = trace_context.get("X-B3-TraceId");
  EXPECT_TRUE(b3_trace_id.has_value());
  EXPECT_EQ("0af7651916cd43dd8448eb211c80319c", b3_trace_id.value());

  auto b3_span_id = trace_context.get("X-B3-SpanId");
  EXPECT_TRUE(b3_span_id.has_value());
  EXPECT_EQ("b7ad6b7169203331", b3_span_id.value());

  auto b3_sampled = trace_context.get("X-B3-Sampled");
  EXPECT_TRUE(b3_sampled.has_value());
  EXPECT_EQ("1", b3_sampled.value());
}

} // namespace OpenTelemetry
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
