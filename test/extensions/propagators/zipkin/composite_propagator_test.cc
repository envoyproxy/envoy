#include "source/extensions/propagators/zipkin/propagator.h"
#include "source/extensions/propagators/zipkin/b3/propagator.h"
#include "source/extensions/propagators/zipkin/w3c/trace_context_propagator.h"

#include "source/extensions/tracers/zipkin/span_context.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace Zipkin {
namespace {

class CompositePropagatorTest : public testing::Test {
public:
  void SetUp() override {
    std::vector<TextMapPropagatorPtr> propagators;
    propagators.push_back(std::make_unique<B3Propagator>());
    propagators.push_back(std::make_unique<W3CTraceContextPropagator>());
    composite_ = std::make_unique<CompositePropagator>(std::move(propagators));
  }

protected:
  std::unique_ptr<CompositePropagator> composite_;
};

TEST_F(CompositePropagatorTest, ExtractB3Headers) {
  Http::TestRequestHeaderMapImpl headers{{"x-b3-traceid", "0000000000000001"},
                                         {"x-b3-spanid", "0000000000000002"},
                                         {"x-b3-sampled", "1"}};
  Tracing::TestTraceContextImpl trace_context{headers};

  EXPECT_TRUE(composite_->propagationHeaderPresent(trace_context));

  auto result = composite_->extract(trace_context);
  EXPECT_TRUE(result.ok());

  const auto& span_context = result.value();
  EXPECT_EQ(span_context.traceId(), 1);
  EXPECT_EQ(span_context.id(), 2);
  EXPECT_TRUE(span_context.sampled());
}

TEST_F(CompositePropagatorTest, ExtractW3CHeaders) {
  Http::TestRequestHeaderMapImpl headers{
      {"traceparent", "00-00000000000000010000000000000002-0000000000000003-01"}};
  Tracing::TestTraceContextImpl trace_context{headers};

  EXPECT_TRUE(composite_->propagationHeaderPresent(trace_context));

  auto result = composite_->extract(trace_context);
  EXPECT_TRUE(result.ok());

  const auto& span_context = result.value();
  EXPECT_EQ(span_context.traceId(), 2);
  EXPECT_TRUE(span_context.sampled());
}

TEST_F(CompositePropagatorTest, ExtractPriority) {
  // Both B3 and W3C headers present - B3 should win (first in list)
  Http::TestRequestHeaderMapImpl headers{
      {"x-b3-traceid", "0000000000000001"},
      {"x-b3-spanid", "0000000000000002"},
      {"x-b3-sampled", "1"},
      {"traceparent", "00-00000000000000030000000000000004-0000000000000005-01"}};
  Tracing::TestTraceContextImpl trace_context{headers};

  EXPECT_TRUE(composite_->propagationHeaderPresent(trace_context));

  auto result = composite_->extract(trace_context);
  EXPECT_TRUE(result.ok());

  const auto& span_context = result.value();
  // Should use B3 values (trace_id = 1), not W3C values (trace_id = 4)
  EXPECT_EQ(span_context.traceId(), 1);
  EXPECT_EQ(span_context.id(), 2);
}

TEST_F(CompositePropagatorTest, ExtractNoHeaders) {
  Http::TestRequestHeaderMapImpl headers{};
  Tracing::TestTraceContextImpl trace_context{headers};

  EXPECT_FALSE(composite_->propagationHeaderPresent(trace_context));

  auto result = composite_->extract(trace_context);
  EXPECT_FALSE(result.ok());
}

TEST_F(CompositePropagatorTest, InjectSpanContext) {
  Extensions::Tracers::Zipkin::SpanContext span_context(1, 2, 3, 4, true);

  Http::TestRequestHeaderMapImpl headers{};
  Tracing::TestTraceContextImpl trace_context{headers};

  composite_->inject(span_context, trace_context);

  // Should inject both B3 and W3C formats
  EXPECT_TRUE(trace_context.getByKey("x-b3-traceid").has_value());
  EXPECT_TRUE(trace_context.getByKey("x-b3-spanid").has_value());
  EXPECT_TRUE(trace_context.getByKey("x-b3-sampled").has_value());
  EXPECT_TRUE(trace_context.getByKey("b3").has_value());
  EXPECT_TRUE(trace_context.getByKey("traceparent").has_value());
}

TEST_F(CompositePropagatorTest, B3SingleHeaderFormat) {
  Http::TestRequestHeaderMapImpl headers{
      {"b3", "0000000000000001-0000000000000002-1-0000000000000004"}};
  Tracing::TestTraceContextImpl trace_context{headers};

  EXPECT_TRUE(composite_->propagationHeaderPresent(trace_context));

  auto result = composite_->extract(trace_context);
  EXPECT_TRUE(result.ok());

  const auto& span_context = result.value();
  EXPECT_EQ(span_context.traceId(), 1);
  EXPECT_EQ(span_context.id(), 2);
  EXPECT_EQ(span_context.parentId(), 4);
  EXPECT_TRUE(span_context.sampled());
}

TEST_F(CompositePropagatorTest, B3SamplingOnlyHeaders) {
  Http::TestRequestHeaderMapImpl headers{{"b3", "1"}};
  Tracing::TestTraceContextImpl trace_context{headers};

  EXPECT_TRUE(composite_->propagationHeaderPresent(trace_context));

  auto result = composite_->extract(trace_context);
  EXPECT_TRUE(result.ok());

  const auto& span_context = result.value();
  EXPECT_TRUE(span_context.sampled());
}

TEST_F(CompositePropagatorTest, B3DebugFlag) {
  Http::TestRequestHeaderMapImpl headers{{"b3", "d"}};
  Tracing::TestTraceContextImpl trace_context{headers};

  EXPECT_TRUE(composite_->propagationHeaderPresent(trace_context));

  auto result = composite_->extract(trace_context);
  EXPECT_TRUE(result.ok());

  const auto& span_context = result.value();
  EXPECT_TRUE(span_context.sampled());
}

} // namespace
} // namespace Zipkin
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
