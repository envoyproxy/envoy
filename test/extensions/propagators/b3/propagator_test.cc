#include "source/extensions/propagators/b3/propagator.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {

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
  EXPECT_EQ(result->traceId(), "00000000000000000000000000000001");
  EXPECT_EQ(result->spanId(), "0000000000000002");
  EXPECT_TRUE(result->sampled());
}

TEST_F(B3PropagatorTest, ExtractFromSingleHeaderFormat) {
  Tracing::TestTraceContextImpl trace_context{{"b3", "0000000000000001-0000000000000002-1"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->traceId(), "00000000000000000000000000000001");
  EXPECT_EQ(result->spanId(), "0000000000000002");
  EXPECT_TRUE(result->sampled());
}

TEST_F(B3PropagatorTest, ExtractNotSampled) {
  Tracing::TestTraceContextImpl trace_context{{"X-B3-TraceId", "0000000000000001"},
                                              {"X-B3-SpanId", "0000000000000002"},
                                              {"X-B3-Sampled", "0"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->traceId(), "00000000000000000000000000000001");
  EXPECT_EQ(result->spanId(), "0000000000000002");
  EXPECT_FALSE(result->sampled());
}

TEST_F(B3PropagatorTest, ExtractWithFlags) {
  Tracing::TestTraceContextImpl trace_context{{"X-B3-TraceId", "0000000000000001"},
                                              {"X-B3-SpanId", "0000000000000002"},
                                              {"X-B3-Flags", "1"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->traceId(), "00000000000000000000000000000001");
  EXPECT_EQ(result->spanId(), "0000000000000002");
  EXPECT_TRUE(result->sampled()); // Debug flag implies sampling
}

TEST_F(B3PropagatorTest, ExtractInvalidTraceId) {
  Tracing::TestTraceContextImpl trace_context{{"X-B3-TraceId", "invalid"},
                                              {"X-B3-SpanId", "0000000000000002"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), HasSubstr("Invalid"));
}

TEST_F(B3PropagatorTest, ExtractMissingHeaders) {
  Tracing::TestTraceContextImpl trace_context{{}};

  auto result = propagator_->extract(trace_context);

  EXPECT_FALSE(result.ok());
}

TEST_F(B3PropagatorTest, InjectBothFormats) {
  Tracers::OpenTelemetry::SpanContext span_context("00", "0af7651916cd43dd8448eb211c80319c",
                                                   "b7ad6b7169203331", true, "");
  Tracing::TestTraceContextImpl trace_context{{}};

  propagator_->inject(span_context, trace_context);

  // Should have single header format
  auto b3_header = trace_context.get("b3");
  EXPECT_TRUE(b3_header.has_value());
  EXPECT_EQ("0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-1", b3_header.value());

  // Should have multi-header format
  auto trace_id = trace_context.get("X-B3-TraceId");
  EXPECT_TRUE(trace_id.has_value());
  EXPECT_EQ("0af7651916cd43dd8448eb211c80319c", trace_id.value());

  auto span_id = trace_context.get("X-B3-SpanId");
  EXPECT_TRUE(span_id.has_value());
  EXPECT_EQ("b7ad6b7169203331", span_id.value());

  auto sampled = trace_context.get("X-B3-Sampled");
  EXPECT_TRUE(sampled.has_value());
  EXPECT_EQ("1", sampled.value());
}

TEST_F(B3PropagatorTest, Fields) {
  auto fields = propagator_->fields();

  EXPECT_THAT(fields, testing::Contains("b3"));
  EXPECT_THAT(fields, testing::Contains("X-B3-TraceId"));
  EXPECT_THAT(fields, testing::Contains("X-B3-SpanId"));
  EXPECT_THAT(fields, testing::Contains("X-B3-Sampled"));
  EXPECT_THAT(fields, testing::Contains("X-B3-Flags"));
  EXPECT_THAT(fields, testing::Contains("X-B3-ParentSpanId"));
}

TEST_F(B3PropagatorTest, ExtractSingleHeaderWithDebugFlag) {
  Tracing::TestTraceContextImpl trace_context{{"b3", "0000000000000001-0000000000000002-d"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->traceId(), "00000000000000000000000000000001");
  EXPECT_EQ(result->spanId(), "0000000000000002");
  EXPECT_TRUE(result->sampled()); // Debug flag implies sampling
}

TEST_F(B3PropagatorTest, ExtractSingleHeaderWithParentAndDebug) {
  Tracing::TestTraceContextImpl trace_context{
      {"b3", "0000000000000001-0000000000000002-d-0000000000000005"}};

  auto result = propagator_->extract(trace_context);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->traceId(), "00000000000000000000000000000001");
  EXPECT_EQ(result->spanId(), "0000000000000002");
  EXPECT_TRUE(result->sampled()); // Debug flag implies sampling
  // Note: Parent ID is not included in OpenTelemetry SpanContext but should be readable from
  // headers
}

TEST_F(B3PropagatorTest, ExtractSamplingOnlyHeadersReturnError) {
  // These should return errors as they don't contain valid trace context
  {
    Tracing::TestTraceContextImpl trace_context{{"b3", "0"}};
    auto result = propagator_->extract(trace_context);
    EXPECT_FALSE(result.ok());
    EXPECT_THAT(result.status().message(), HasSubstr("not sampled"));
  }

  {
    Tracing::TestTraceContextImpl trace_context{{"b3", "1"}};
    auto result = propagator_->extract(trace_context);
    EXPECT_FALSE(result.ok());
    EXPECT_THAT(result.status().message(), HasSubstr("debug flag"));
  }

  {
    Tracing::TestTraceContextImpl trace_context{{"b3", "d"}};
    auto result = propagator_->extract(trace_context);
    EXPECT_FALSE(result.ok());
    EXPECT_THAT(result.status().message(), HasSubstr("debug flag"));
  }
}

TEST_F(B3PropagatorTest, Name) { EXPECT_EQ("b3", propagator_->name()); }

} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
