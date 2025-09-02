#include "source/extensions/tracers/opentelemetry/propagators/w3c/w3c_trace_context_propagator.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

using testing::HasSubstr;

class W3CTraceContextPropagatorTest : public testing::Test {
public:
  W3CTraceContextPropagatorTest() : propagator_(std::make_unique<W3CTraceContextPropagator>()) {}

protected:
  std::unique_ptr<W3CTraceContextPropagator> propagator_;
};

TEST_F(W3CTraceContextPropagatorTest, ExtractValidTraceparent) {
  Tracing::TestTraceContextImpl trace_context{
      {"traceparent", "00-00000000000000000000000000000001-0000000000000002-01"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->traceId(), "00000000000000000000000000000001");
  EXPECT_EQ(result->spanId(), "0000000000000002");
  EXPECT_TRUE(result->sampled());
}

TEST_F(W3CTraceContextPropagatorTest, ExtractWithTracestate) {
  Tracing::TestTraceContextImpl trace_context{
      {"traceparent", "00-00000000000000000000000000000001-0000000000000002-01"},
      {"tracestate", "vendor1=value1,vendor2=value2"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->tracestate(), "vendor1=value1,vendor2=value2");
}

TEST_F(W3CTraceContextPropagatorTest, ExtractNotSampled) {
  Tracing::TestTraceContextImpl trace_context{
      {"traceparent", "00-00000000000000000000000000000001-0000000000000002-00"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_TRUE(result.ok());
  EXPECT_FALSE(result->sampled());
}

TEST_F(W3CTraceContextPropagatorTest, ExtractFailsWithMissingTraceparent) {
  Tracing::TestTraceContextImpl trace_context{{"tracestate", "vendor1=value1"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), HasSubstr("traceparent header not found"));
}

TEST_F(W3CTraceContextPropagatorTest, ExtractFailsWithInvalidLength) {
  Tracing::TestTraceContextImpl trace_context{{"traceparent", "00-001-002-01"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), HasSubstr("Invalid traceparent header length"));
}

TEST_F(W3CTraceContextPropagatorTest, ExtractFailsWithAllZeroTraceId) {
  Tracing::TestTraceContextImpl trace_context{
      {"traceparent", "00-00000000000000000000000000000000-0000000000000002-01"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), HasSubstr("Invalid trace id"));
}

TEST_F(W3CTraceContextPropagatorTest, ExtractFailsWithAllZeroSpanId) {
  Tracing::TestTraceContextImpl trace_context{
      {"traceparent", "00-00000000000000000000000000000001-0000000000000000-01"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), HasSubstr("Invalid parent id"));
}

TEST_F(W3CTraceContextPropagatorTest, InjectTraceparentAndTracestate) {
  SpanContext span_context("00000000000000000000000000000001", "0000000000000002", true,
                           "vendor1=value1");
  Tracing::TestTraceContextImpl trace_context{};

  propagator_->inject(span_context, trace_context);

  EXPECT_EQ(trace_context.get("traceparent"),
            "00-00000000000000000000000000000001-0000000000000002-01");
  EXPECT_EQ(trace_context.get("tracestate"), "vendor1=value1");
}

TEST_F(W3CTraceContextPropagatorTest, InjectNotSampled) {
  SpanContext span_context("00000000000000000000000000000001", "0000000000000002", false, "");
  Tracing::TestTraceContextImpl trace_context{};

  propagator_->inject(span_context, trace_context);

  EXPECT_EQ(trace_context.get("traceparent"),
            "00-00000000000000000000000000000001-0000000000000002-00");
}

TEST_F(W3CTraceContextPropagatorTest, FieldsReturnsExpectedHeaders) {
  auto fields = propagator_->fields();

  EXPECT_EQ(fields.size(), 2);
  EXPECT_THAT(fields, testing::Contains("traceparent"));
  EXPECT_THAT(fields, testing::Contains("tracestate"));
}

TEST_F(W3CTraceContextPropagatorTest, NameReturnsTracecontext) {
  EXPECT_EQ(propagator_->name(), "tracecontext");
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
