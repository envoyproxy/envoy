#include "zipkin/span_context.h"
#include "zipkin/zipkin_core_constants.h"

#include "gtest/gtest.h"

namespace Zipkin {

TEST(ZipkinSpanContextTest, populateFromString) {
  SpanContext spanContext;

  // Non-initialized span context
  EXPECT_EQ(0ULL, spanContext.trace_id());
  EXPECT_EQ("0000000000000000", spanContext.traceIdAsHexString());
  EXPECT_EQ(0ULL, spanContext.id());
  EXPECT_EQ("0000000000000000", spanContext.idAsHexString());
  EXPECT_EQ(0ULL, spanContext.parent_id());
  EXPECT_EQ("0000000000000000", spanContext.parentIdAsHexString());
  EXPECT_FALSE(spanContext.isSetAnnotation().cr);
  EXPECT_FALSE(spanContext.isSetAnnotation().cs);
  EXPECT_FALSE(spanContext.isSetAnnotation().sr);
  EXPECT_FALSE(spanContext.isSetAnnotation().ss);
  EXPECT_EQ("0000000000000000;0000000000000000;0000000000000000", spanContext.serializeToString());

  // Span context populated with trace id, id, parent id, and no annotations
  spanContext.populateFromString("25c6f38dd0600e79;56707c7b3e1092af;c49193ea42335d1c");
  EXPECT_EQ(2722130815203937913ULL, spanContext.trace_id());
  EXPECT_EQ("25c6f38dd0600e79", spanContext.traceIdAsHexString());
  EXPECT_EQ(6228615153417491119ULL, spanContext.id());
  EXPECT_EQ("56707c7b3e1092af", spanContext.idAsHexString());
  EXPECT_EQ(14164264937399213340ULL, spanContext.parent_id());
  EXPECT_EQ("c49193ea42335d1c", spanContext.parentIdAsHexString());
  EXPECT_FALSE(spanContext.isSetAnnotation().cr);
  EXPECT_FALSE(spanContext.isSetAnnotation().cs);
  EXPECT_FALSE(spanContext.isSetAnnotation().sr);
  EXPECT_FALSE(spanContext.isSetAnnotation().ss);
  EXPECT_EQ("25c6f38dd0600e79;56707c7b3e1092af;c49193ea42335d1c", spanContext.serializeToString());

  // Span context populated with trace id, id, parent id, and one annotation
  spanContext.populateFromString("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;cs");
  EXPECT_EQ(2722130815203937912ULL, spanContext.trace_id());
  EXPECT_EQ("25c6f38dd0600e78", spanContext.traceIdAsHexString());
  EXPECT_EQ(6228615153417491119ULL, spanContext.id());
  EXPECT_EQ("56707c7b3e1092af", spanContext.idAsHexString());
  EXPECT_EQ(14164264937399213340ULL, spanContext.parent_id());
  EXPECT_EQ("c49193ea42335d1c", spanContext.parentIdAsHexString());
  EXPECT_FALSE(spanContext.isSetAnnotation().cr);
  EXPECT_TRUE(spanContext.isSetAnnotation().cs);
  EXPECT_FALSE(spanContext.isSetAnnotation().sr);
  EXPECT_FALSE(spanContext.isSetAnnotation().ss);
  EXPECT_EQ("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;cs",
            spanContext.serializeToString());

  // Span context populated with trace id, id, parent id, and multiple annotations
  spanContext.populateFromString("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;cs;cr");
  EXPECT_EQ(2722130815203937912ULL, spanContext.trace_id());
  EXPECT_EQ("25c6f38dd0600e78", spanContext.traceIdAsHexString());
  EXPECT_EQ(6228615153417491119ULL, spanContext.id());
  EXPECT_EQ("56707c7b3e1092af", spanContext.idAsHexString());
  EXPECT_EQ(14164264937399213340ULL, spanContext.parent_id());
  EXPECT_EQ("c49193ea42335d1c", spanContext.parentIdAsHexString());
  EXPECT_TRUE(spanContext.isSetAnnotation().cr);
  EXPECT_TRUE(spanContext.isSetAnnotation().cs);
  EXPECT_FALSE(spanContext.isSetAnnotation().sr);
  EXPECT_FALSE(spanContext.isSetAnnotation().ss);
  EXPECT_EQ("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;cr;cs",
            spanContext.serializeToString());

  // Span context populated with invalid string: it gets reset to its non-initialized state
  spanContext.populateFromString("invalid string");
  EXPECT_EQ(0ULL, spanContext.trace_id());
  EXPECT_EQ("0000000000000000", spanContext.traceIdAsHexString());
  EXPECT_EQ(0ULL, spanContext.id());
  EXPECT_EQ("0000000000000000", spanContext.idAsHexString());
  EXPECT_EQ(0ULL, spanContext.parent_id());
  EXPECT_EQ("0000000000000000", spanContext.parentIdAsHexString());
  EXPECT_FALSE(spanContext.isSetAnnotation().cr);
  EXPECT_FALSE(spanContext.isSetAnnotation().cs);
  EXPECT_FALSE(spanContext.isSetAnnotation().sr);
  EXPECT_FALSE(spanContext.isSetAnnotation().ss);
  EXPECT_EQ("0000000000000000;0000000000000000;0000000000000000", spanContext.serializeToString());
}

TEST(ZipkinSpanContextTest, populateFromSpan) {
  Span span;
  SpanContext spanContext(span);

  // Non-initialized span context
  EXPECT_EQ(0ULL, spanContext.trace_id());
  EXPECT_EQ("0000000000000000", spanContext.traceIdAsHexString());
  EXPECT_EQ(0ULL, spanContext.id());
  EXPECT_EQ("0000000000000000", spanContext.idAsHexString());
  EXPECT_EQ(0ULL, spanContext.parent_id());
  EXPECT_EQ("0000000000000000", spanContext.parentIdAsHexString());
  EXPECT_FALSE(spanContext.isSetAnnotation().cr);
  EXPECT_FALSE(spanContext.isSetAnnotation().cs);
  EXPECT_FALSE(spanContext.isSetAnnotation().sr);
  EXPECT_FALSE(spanContext.isSetAnnotation().ss);
  EXPECT_EQ("0000000000000000;0000000000000000;0000000000000000", spanContext.serializeToString());

  // Span context populated with trace id, id, parent id, and no annotations
  span.setTraceId(2722130815203937912ULL);
  span.setId(6228615153417491119ULL);
  span.setParentId(14164264937399213340ULL);
  SpanContext spanContext2(span);
  EXPECT_EQ(2722130815203937912ULL, spanContext2.trace_id());
  EXPECT_EQ("25c6f38dd0600e78", spanContext2.traceIdAsHexString());
  EXPECT_EQ(6228615153417491119ULL, spanContext2.id());
  EXPECT_EQ("56707c7b3e1092af", spanContext2.idAsHexString());
  EXPECT_EQ(14164264937399213340ULL, spanContext2.parent_id());
  EXPECT_EQ("c49193ea42335d1c", spanContext2.parentIdAsHexString());
  EXPECT_FALSE(spanContext2.isSetAnnotation().cr);
  EXPECT_FALSE(spanContext2.isSetAnnotation().cs);
  EXPECT_FALSE(spanContext2.isSetAnnotation().sr);
  EXPECT_FALSE(spanContext2.isSetAnnotation().ss);
  EXPECT_EQ("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c", spanContext2.serializeToString());

  // Test if we can handle 128-bit trace ids
  EXPECT_FALSE(span.isSet().trace_id_high);
  span.setTraceIdHigh(9922130815203937912ULL);
  EXPECT_TRUE(span.isSet().trace_id_high);
  SpanContext spanContext5(span);
  // We currently drop the high bits. So, we expect the same context as above
  EXPECT_EQ("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c", spanContext5.serializeToString());

  // Span context populated with trace id, id, parent id, and one annotation
  Annotation ann;
  ann.setValue(ZipkinCoreConstants::SERVER_RECV);
  span.addAnnotation(ann);
  SpanContext spanContext3(span);
  EXPECT_EQ(2722130815203937912ULL, spanContext3.trace_id());
  EXPECT_EQ("25c6f38dd0600e78", spanContext3.traceIdAsHexString());
  EXPECT_EQ(6228615153417491119ULL, spanContext3.id());
  EXPECT_EQ("56707c7b3e1092af", spanContext3.idAsHexString());
  EXPECT_EQ(14164264937399213340ULL, spanContext3.parent_id());
  EXPECT_EQ("c49193ea42335d1c", spanContext3.parentIdAsHexString());
  EXPECT_FALSE(spanContext3.isSetAnnotation().cr);
  EXPECT_FALSE(spanContext3.isSetAnnotation().cs);
  EXPECT_TRUE(spanContext3.isSetAnnotation().sr);
  EXPECT_FALSE(spanContext3.isSetAnnotation().ss);
  EXPECT_EQ("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;sr",
            spanContext3.serializeToString());

  // Span context populated with trace id, id, parent id, and multiple annotations
  ann.setValue(ZipkinCoreConstants::SERVER_SEND);
  span.addAnnotation(ann);
  SpanContext spanContext4(span);
  EXPECT_EQ(2722130815203937912ULL, spanContext4.trace_id());
  EXPECT_EQ("25c6f38dd0600e78", spanContext4.traceIdAsHexString());
  EXPECT_EQ(6228615153417491119ULL, spanContext4.id());
  EXPECT_EQ("56707c7b3e1092af", spanContext4.idAsHexString());
  EXPECT_EQ(14164264937399213340ULL, spanContext4.parent_id());
  EXPECT_EQ("c49193ea42335d1c", spanContext4.parentIdAsHexString());
  EXPECT_FALSE(spanContext4.isSetAnnotation().cr);
  EXPECT_FALSE(spanContext4.isSetAnnotation().cs);
  EXPECT_TRUE(spanContext4.isSetAnnotation().sr);
  EXPECT_TRUE(spanContext4.isSetAnnotation().ss);
  EXPECT_EQ("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;sr;ss",
            spanContext4.serializeToString());
}
} // Zipkin
