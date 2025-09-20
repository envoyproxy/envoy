#include "source/extensions/propagators/w3c/trace_context_propagator.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {

using testing::HasSubstr;

class W3CTraceContextPropagatorTest : public testing::Test {
public:
  W3CTraceContextPropagatorTest() : propagator_(std::make_unique<W3CTraceContextPropagator>()) {}

protected:
  std::unique_ptr<W3CTraceContextPropagator> propagator_;
};

TEST_F(W3CTraceContextPropagatorTest, ExtractValid) {
  Tracing::TestTraceContextImpl trace_context{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->traceId(), "0af7651916cd43dd8448eb211c80319c");
  EXPECT_EQ(result->spanId(), "b7ad6b7169203331");
  EXPECT_TRUE(result->sampled());
}

TEST_F(W3CTraceContextPropagatorTest, ExtractNotSampled) {
  Tracing::TestTraceContextImpl trace_context{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-00"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->traceId(), "0af7651916cd43dd8448eb211c80319c");
  EXPECT_EQ(result->spanId(), "b7ad6b7169203331");
  EXPECT_FALSE(result->sampled());
}

TEST_F(W3CTraceContextPropagatorTest, ExtractWithTraceState) {
  Tracing::TestTraceContextImpl trace_context{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"},
      {"tracestate", "rojo=00f067aa0ba902b7,congo=t61rcWkgMzE"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->traceId(), "0af7651916cd43dd8448eb211c80319c");
  EXPECT_EQ(result->spanId(), "b7ad6b7169203331");
  EXPECT_TRUE(result->sampled());
  EXPECT_EQ(result->traceState(), "rojo=00f067aa0ba902b7,congo=t61rcWkgMzE");
}

TEST_F(W3CTraceContextPropagatorTest, ExtractMissingHeader) {
  Tracing::TestTraceContextImpl trace_context{{}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), HasSubstr("No traceparent header"));
}

TEST_F(W3CTraceContextPropagatorTest, ExtractInvalidLength) {
  Tracing::TestTraceContextImpl trace_context{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), HasSubstr("Invalid traceparent header length"));
}

TEST_F(W3CTraceContextPropagatorTest, ExtractInvalidFormat) {
  Tracing::TestTraceContextImpl trace_context{
      {"traceparent", "invalid-format-header-value-0000000000000000000000000000"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), HasSubstr("Invalid traceparent header"));
}

TEST_F(W3CTraceContextPropagatorTest, ExtractAllZeroTraceId) {
  Tracing::TestTraceContextImpl trace_context{
      {"traceparent", "00-00000000000000000000000000000000-b7ad6b7169203331-01"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), HasSubstr("Invalid traceparent header values"));
}

TEST_F(W3CTraceContextPropagatorTest, ExtractAllZeroSpanId) {
  Tracing::TestTraceContextImpl trace_context{
      {"traceparent", "00-0af7651916cd43dd8448eb211c80319c-0000000000000000-01"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), HasSubstr("Invalid traceparent header values"));
}

TEST_F(W3CTraceContextPropagatorTest, Inject) {
  Tracers::OpenTelemetry::SpanContext span_context(
      "00", "0af7651916cd43dd8448eb211c80319c", "b7ad6b7169203331", true, "rojo=00f067aa0ba902b7");
  Tracing::TestTraceContextImpl trace_context{{}};

  propagator_->inject(span_context, trace_context);

  auto traceparent = trace_context.get("traceparent");
  EXPECT_TRUE(traceparent.has_value());
  EXPECT_EQ("00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01", traceparent.value());

  auto tracestate = trace_context.get("tracestate");
  EXPECT_TRUE(tracestate.has_value());
  EXPECT_EQ("rojo=00f067aa0ba902b7", tracestate.value());
}

TEST_F(W3CTraceContextPropagatorTest, InjectNotSampled) {
  Tracers::OpenTelemetry::SpanContext span_context("00", "0af7651916cd43dd8448eb211c80319c",
                                                   "b7ad6b7169203331", false, "");
  Tracing::TestTraceContextImpl trace_context{{}};

  propagator_->inject(span_context, trace_context);

  auto traceparent = trace_context.get("traceparent");
  EXPECT_TRUE(traceparent.has_value());
  EXPECT_EQ("00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-00", traceparent.value());
}

TEST_F(W3CTraceContextPropagatorTest, Fields) {
  auto fields = propagator_->fields();

  EXPECT_THAT(fields, testing::Contains("traceparent"));
  EXPECT_THAT(fields, testing::Contains("tracestate"));
}

TEST_F(W3CTraceContextPropagatorTest, Name) { EXPECT_EQ("tracecontext", propagator_->name()); }

} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
