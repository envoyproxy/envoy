#include "common/tracing/zipkin/span_context.h"
#include "common/tracing/zipkin/zipkin_core_constants.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Zipkin {

TEST(ZipkinSpanContextTest, populateFromString) {
  SpanContext span_context;

  // Non-initialized span context
  EXPECT_EQ(0ULL, span_context.trace_id());
  EXPECT_EQ("0000000000000000", span_context.traceIdAsHexString());
  EXPECT_EQ(0ULL, span_context.id());
  EXPECT_EQ("0000000000000000", span_context.idAsHexString());
  EXPECT_EQ(0ULL, span_context.parent_id());
  EXPECT_EQ("0000000000000000", span_context.parentIdAsHexString());
  EXPECT_EQ("0000000000000000;0000000000000000;0000000000000000", span_context.serializeToString());

  // Span context populated with trace id, id and parent id
  span_context.populateFromString("25c6f38dd0600e79;56707c7b3e1092af;c49193ea42335d1c");
  EXPECT_EQ(2722130815203937913ULL, span_context.trace_id());
  EXPECT_EQ("25c6f38dd0600e79", span_context.traceIdAsHexString());
  EXPECT_EQ(6228615153417491119ULL, span_context.id());
  EXPECT_EQ("56707c7b3e1092af", span_context.idAsHexString());
  EXPECT_EQ(14164264937399213340ULL, span_context.parent_id());
  EXPECT_EQ("c49193ea42335d1c", span_context.parentIdAsHexString());
  EXPECT_EQ("25c6f38dd0600e79;56707c7b3e1092af;c49193ea42335d1c", span_context.serializeToString());

  // Span context populated with invalid string: it gets reset to its non-initialized state
  span_context.populateFromString("invalid string");
  EXPECT_EQ(0ULL, span_context.trace_id());
  EXPECT_EQ("0000000000000000", span_context.traceIdAsHexString());
  EXPECT_EQ(0ULL, span_context.id());
  EXPECT_EQ("0000000000000000", span_context.idAsHexString());
  EXPECT_EQ(0ULL, span_context.parent_id());
  EXPECT_EQ("0000000000000000", span_context.parentIdAsHexString());
  EXPECT_EQ("0000000000000000;0000000000000000;0000000000000000", span_context.serializeToString());
}

TEST(ZipkinSpanContextTest, populateFromSpan) {
  Span span;
  SpanContext span_context(span);

  // Non-initialized span context
  EXPECT_EQ(0ULL, span_context.trace_id());
  EXPECT_EQ("0000000000000000", span_context.traceIdAsHexString());
  EXPECT_EQ(0ULL, span_context.id());
  EXPECT_EQ("0000000000000000", span_context.idAsHexString());
  EXPECT_EQ(0ULL, span_context.parent_id());
  EXPECT_EQ("0000000000000000", span_context.parentIdAsHexString());
  EXPECT_EQ("0000000000000000;0000000000000000;0000000000000000", span_context.serializeToString());

  // Span context populated with trace id, id and parent id
  span.setTraceId(2722130815203937912ULL);
  span.setId(6228615153417491119ULL);
  span.setParentId(14164264937399213340ULL);
  SpanContext span_context_2(span);
  EXPECT_EQ(2722130815203937912ULL, span_context_2.trace_id());
  EXPECT_EQ("25c6f38dd0600e78", span_context_2.traceIdAsHexString());
  EXPECT_EQ(6228615153417491119ULL, span_context_2.id());
  EXPECT_EQ("56707c7b3e1092af", span_context_2.idAsHexString());
  EXPECT_EQ(14164264937399213340ULL, span_context_2.parent_id());
  EXPECT_EQ("c49193ea42335d1c", span_context_2.parentIdAsHexString());
  EXPECT_EQ("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c",
            span_context_2.serializeToString());

  // Test if we can handle 128-bit trace ids
  EXPECT_FALSE(span.isSetTraceIdHigh());
  span.setTraceIdHigh(9922130815203937912ULL);
  EXPECT_TRUE(span.isSetTraceIdHigh());
  SpanContext span_context_high_id(span);
  // We currently drop the high bits. So, we expect the same context as above
  EXPECT_EQ("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c",
            span_context_high_id.serializeToString());
}
} // namespace Zipkin
} // namespace Envoy
