#include "source/extensions/tracers/opentelemetry/propagators/b3/b3_propagator.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

using testing::HasSubstr;

class B3PropagatorTest : public testing::Test {
public:
  B3PropagatorTest() : propagator_(std::make_unique<B3Propagator>()) {}

protected:
  std::unique_ptr<B3Propagator> propagator_;
};

TEST_F(B3PropagatorTest, ExtractFromMultiHeaderFormat) {
  Tracing::TestTraceContextImpl trace_context{{"X-B3-TraceId", "0000000000000001"},
                                              {"X-B3-SpanId", "0000000000000002"},
                                              {"X-B3-Sampled", "1"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->traceId(), "0000000000000001");
  EXPECT_EQ(result->spanId(), "0000000000000002");
  EXPECT_TRUE(result->sampled());
}

TEST_F(B3PropagatorTest, ExtractFromSingleHeaderFormat) {
  Tracing::TestTraceContextImpl trace_context{{"b3", "0000000000000001-0000000000000002-1"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->traceId(), "0000000000000001");
  EXPECT_EQ(result->spanId(), "0000000000000002");
  EXPECT_TRUE(result->sampled());
}

TEST_F(B3PropagatorTest, ExtractNotSampled) {
  Tracing::TestTraceContextImpl trace_context{{"X-B3-TraceId", "0000000000000001"},
                                              {"X-B3-SpanId", "0000000000000002"},
                                              {"X-B3-Sampled", "0"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_TRUE(result.ok());
  EXPECT_FALSE(result->sampled());
}

TEST_F(B3PropagatorTest, ExtractFailsWithMissingHeaders) {
  Tracing::TestTraceContextImpl trace_context{{"X-B3-TraceId", "0000000000000001"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), HasSubstr("Missing required B3 headers"));
}

TEST_F(B3PropagatorTest, ExtractFailsWithInvalidTraceId) {
  Tracing::TestTraceContextImpl trace_context{
      {"X-B3-TraceId", "invalid_id"}, {"X-B3-SpanId", "0000000000000002"}, {"X-B3-Sampled", "1"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), HasSubstr("Invalid B3 trace ID"));
}

TEST_F(B3PropagatorTest, InjectBothFormats) {
  SpanContext span_context("0000000000000001", "0000000000000002", true, "");
  Tracing::TestTraceContextImpl trace_context{};

  propagator_->inject(span_context, trace_context);

  // Should inject both single-header and multi-header formats
  EXPECT_EQ(trace_context.get("b3"), "0000000000000001-0000000000000002-1");
  EXPECT_EQ(trace_context.get("X-B3-TraceId"), "0000000000000001");
  EXPECT_EQ(trace_context.get("X-B3-SpanId"), "0000000000000002");
  EXPECT_EQ(trace_context.get("X-B3-Sampled"), "1");
}

TEST_F(B3PropagatorTest, InjectNotSampled) {
  SpanContext span_context("0000000000000001", "0000000000000002", false, "");
  Tracing::TestTraceContextImpl trace_context{};

  propagator_->inject(span_context, trace_context);

  EXPECT_EQ(trace_context.get("b3"), "0000000000000001-0000000000000002-0");
  EXPECT_EQ(trace_context.get("X-B3-Sampled"), "0");
}

TEST_F(B3PropagatorTest, FieldsReturnsExpectedHeaders) {
  auto fields = propagator_->fields();

  EXPECT_EQ(fields.size(), 4);
  EXPECT_THAT(fields, testing::Contains("b3"));
  EXPECT_THAT(fields, testing::Contains("X-B3-TraceId"));
  EXPECT_THAT(fields, testing::Contains("X-B3-SpanId"));
  EXPECT_THAT(fields, testing::Contains("X-B3-Sampled"));
}

TEST_F(B3PropagatorTest, NameReturnsB3) { EXPECT_EQ(propagator_->name(), "b3"); }

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
