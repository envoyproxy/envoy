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
  EXPECT_FALSE(span_context.annotationSet().cr_);
  EXPECT_FALSE(span_context.annotationSet().cs_);
  EXPECT_FALSE(span_context.annotationSet().sr_);
  EXPECT_FALSE(span_context.annotationSet().ss_);
  EXPECT_EQ("0000000000000000;0000000000000000;0000000000000000", span_context.serializeToString());

  // Span context populated with trace id, id, parent id, and no annotations
  span_context.populateFromString("25c6f38dd0600e79;56707c7b3e1092af;c49193ea42335d1c");
  EXPECT_EQ(2722130815203937913ULL, span_context.trace_id());
  EXPECT_EQ("25c6f38dd0600e79", span_context.traceIdAsHexString());
  EXPECT_EQ(6228615153417491119ULL, span_context.id());
  EXPECT_EQ("56707c7b3e1092af", span_context.idAsHexString());
  EXPECT_EQ(14164264937399213340ULL, span_context.parent_id());
  EXPECT_EQ("c49193ea42335d1c", span_context.parentIdAsHexString());
  EXPECT_FALSE(span_context.annotationSet().cr_);
  EXPECT_FALSE(span_context.annotationSet().cs_);
  EXPECT_FALSE(span_context.annotationSet().sr_);
  EXPECT_FALSE(span_context.annotationSet().ss_);
  EXPECT_EQ("25c6f38dd0600e79;56707c7b3e1092af;c49193ea42335d1c", span_context.serializeToString());

  // Span context populated with trace id, id, parent id, and one annotation
  span_context.populateFromString("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;cs");
  EXPECT_EQ(2722130815203937912ULL, span_context.trace_id());
  EXPECT_EQ("25c6f38dd0600e78", span_context.traceIdAsHexString());
  EXPECT_EQ(6228615153417491119ULL, span_context.id());
  EXPECT_EQ("56707c7b3e1092af", span_context.idAsHexString());
  EXPECT_EQ(14164264937399213340ULL, span_context.parent_id());
  EXPECT_EQ("c49193ea42335d1c", span_context.parentIdAsHexString());
  EXPECT_FALSE(span_context.annotationSet().cr_);
  EXPECT_TRUE(span_context.annotationSet().cs_);
  EXPECT_FALSE(span_context.annotationSet().sr_);
  EXPECT_FALSE(span_context.annotationSet().ss_);
  EXPECT_EQ("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;cs",
            span_context.serializeToString());

  // Span context populated with trace id, id, parent id, and two annotations
  span_context.populateFromString("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;cs;cr");
  EXPECT_EQ(2722130815203937912ULL, span_context.trace_id());
  EXPECT_EQ("25c6f38dd0600e78", span_context.traceIdAsHexString());
  EXPECT_EQ(6228615153417491119ULL, span_context.id());
  EXPECT_EQ("56707c7b3e1092af", span_context.idAsHexString());
  EXPECT_EQ(14164264937399213340ULL, span_context.parent_id());
  EXPECT_EQ("c49193ea42335d1c", span_context.parentIdAsHexString());
  EXPECT_TRUE(span_context.annotationSet().cr_);
  EXPECT_TRUE(span_context.annotationSet().cs_);
  EXPECT_FALSE(span_context.annotationSet().sr_);
  EXPECT_FALSE(span_context.annotationSet().ss_);
  EXPECT_EQ("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;cr;cs",
            span_context.serializeToString());

  // Span context populated with trace id, id, parent id, and three annotations
  span_context.populateFromString("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;cs;cr;ss");
  EXPECT_EQ(2722130815203937912ULL, span_context.trace_id());
  EXPECT_EQ("25c6f38dd0600e78", span_context.traceIdAsHexString());
  EXPECT_EQ(6228615153417491119ULL, span_context.id());
  EXPECT_EQ("56707c7b3e1092af", span_context.idAsHexString());
  EXPECT_EQ(14164264937399213340ULL, span_context.parent_id());
  EXPECT_EQ("c49193ea42335d1c", span_context.parentIdAsHexString());
  EXPECT_TRUE(span_context.annotationSet().cr_);
  EXPECT_TRUE(span_context.annotationSet().cs_);
  EXPECT_FALSE(span_context.annotationSet().sr_);
  EXPECT_TRUE(span_context.annotationSet().ss_);
  EXPECT_EQ("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;cr;cs;ss",
            span_context.serializeToString());

  // Span context populated with trace id, id, parent id, and four annotations
  span_context.populateFromString("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;cs;cr;ss;sr");
  EXPECT_EQ(2722130815203937912ULL, span_context.trace_id());
  EXPECT_EQ("25c6f38dd0600e78", span_context.traceIdAsHexString());
  EXPECT_EQ(6228615153417491119ULL, span_context.id());
  EXPECT_EQ("56707c7b3e1092af", span_context.idAsHexString());
  EXPECT_EQ(14164264937399213340ULL, span_context.parent_id());
  EXPECT_EQ("c49193ea42335d1c", span_context.parentIdAsHexString());
  EXPECT_TRUE(span_context.annotationSet().cr_);
  EXPECT_TRUE(span_context.annotationSet().cs_);
  EXPECT_TRUE(span_context.annotationSet().sr_);
  EXPECT_TRUE(span_context.annotationSet().ss_);
  EXPECT_EQ("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;cr;cs;sr;ss",
            span_context.serializeToString());

  // Span context populated with invalid string: it gets reset to its non-initialized state
  span_context.populateFromString("invalid string");
  EXPECT_EQ(0ULL, span_context.trace_id());
  EXPECT_EQ("0000000000000000", span_context.traceIdAsHexString());
  EXPECT_EQ(0ULL, span_context.id());
  EXPECT_EQ("0000000000000000", span_context.idAsHexString());
  EXPECT_EQ(0ULL, span_context.parent_id());
  EXPECT_EQ("0000000000000000", span_context.parentIdAsHexString());
  EXPECT_FALSE(span_context.annotationSet().cr_);
  EXPECT_FALSE(span_context.annotationSet().cs_);
  EXPECT_FALSE(span_context.annotationSet().sr_);
  EXPECT_FALSE(span_context.annotationSet().ss_);
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
  EXPECT_FALSE(span_context.annotationSet().cr_);
  EXPECT_FALSE(span_context.annotationSet().cs_);
  EXPECT_FALSE(span_context.annotationSet().sr_);
  EXPECT_FALSE(span_context.annotationSet().ss_);
  EXPECT_EQ("0000000000000000;0000000000000000;0000000000000000", span_context.serializeToString());

  // Span context populated with trace id, id, parent id, and no annotations
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
  EXPECT_FALSE(span_context_2.annotationSet().cr_);
  EXPECT_FALSE(span_context_2.annotationSet().cs_);
  EXPECT_FALSE(span_context_2.annotationSet().sr_);
  EXPECT_FALSE(span_context_2.annotationSet().ss_);
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

  // Span context populated with trace id, id, parent id, and one annotation
  Annotation ann;
  ann.setValue(ZipkinCoreConstants::get().SERVER_RECV);
  span.addAnnotation(ann);
  SpanContext span_context_3(span);
  EXPECT_EQ(2722130815203937912ULL, span_context_3.trace_id());
  EXPECT_EQ("25c6f38dd0600e78", span_context_3.traceIdAsHexString());
  EXPECT_EQ(6228615153417491119ULL, span_context_3.id());
  EXPECT_EQ("56707c7b3e1092af", span_context_3.idAsHexString());
  EXPECT_EQ(14164264937399213340ULL, span_context_3.parent_id());
  EXPECT_EQ("c49193ea42335d1c", span_context_3.parentIdAsHexString());
  EXPECT_FALSE(span_context_3.annotationSet().cr_);
  EXPECT_FALSE(span_context_3.annotationSet().cs_);
  EXPECT_TRUE(span_context_3.annotationSet().sr_);
  EXPECT_FALSE(span_context_3.annotationSet().ss_);
  EXPECT_EQ("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;sr",
            span_context_3.serializeToString());

  // Span context populated with trace id, id, parent id, and two annotations
  ann.setValue(ZipkinCoreConstants::get().SERVER_SEND);
  span.addAnnotation(ann);
  SpanContext span_context_4(span);
  EXPECT_EQ(2722130815203937912ULL, span_context_4.trace_id());
  EXPECT_EQ("25c6f38dd0600e78", span_context_4.traceIdAsHexString());
  EXPECT_EQ(6228615153417491119ULL, span_context_4.id());
  EXPECT_EQ("56707c7b3e1092af", span_context_4.idAsHexString());
  EXPECT_EQ(14164264937399213340ULL, span_context_4.parent_id());
  EXPECT_EQ("c49193ea42335d1c", span_context_4.parentIdAsHexString());
  EXPECT_FALSE(span_context_4.annotationSet().cr_);
  EXPECT_FALSE(span_context_4.annotationSet().cs_);
  EXPECT_TRUE(span_context_4.annotationSet().sr_);
  EXPECT_TRUE(span_context_4.annotationSet().ss_);
  EXPECT_EQ("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;sr;ss",
            span_context_4.serializeToString());

  // Span context populated with trace id, id, parent id, and three annotations
  ann.setValue(ZipkinCoreConstants::get().CLIENT_SEND);
  span.addAnnotation(ann);
  SpanContext span_context_5(span);
  EXPECT_EQ(2722130815203937912ULL, span_context_5.trace_id());
  EXPECT_EQ("25c6f38dd0600e78", span_context_5.traceIdAsHexString());
  EXPECT_EQ(6228615153417491119ULL, span_context_5.id());
  EXPECT_EQ("56707c7b3e1092af", span_context_5.idAsHexString());
  EXPECT_EQ(14164264937399213340ULL, span_context_5.parent_id());
  EXPECT_EQ("c49193ea42335d1c", span_context_5.parentIdAsHexString());
  EXPECT_FALSE(span_context_5.annotationSet().cr_);
  EXPECT_TRUE(span_context_5.annotationSet().cs_);
  EXPECT_TRUE(span_context_5.annotationSet().sr_);
  EXPECT_TRUE(span_context_5.annotationSet().ss_);
  EXPECT_EQ("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;cs;sr;ss",
            span_context_5.serializeToString());

  // Span context populated with trace id, id, parent id, and four annotations
  std::vector<Annotation> annotations;
  annotations.push_back(ann);
  ann.setValue(ZipkinCoreConstants::get().SERVER_SEND);
  annotations.push_back(ann);
  ann.setValue(ZipkinCoreConstants::get().SERVER_RECV);
  annotations.push_back(ann);
  ann.setValue(ZipkinCoreConstants::get().CLIENT_RECV);
  annotations.push_back(ann);
  ann.setValue(ZipkinCoreConstants::get().CLIENT_SEND);
  annotations.push_back(ann);
  span.setAnnotations(annotations);
  SpanContext span_context_6(span);
  EXPECT_EQ(2722130815203937912ULL, span_context_6.trace_id());
  EXPECT_EQ("25c6f38dd0600e78", span_context_6.traceIdAsHexString());
  EXPECT_EQ(6228615153417491119ULL, span_context_6.id());
  EXPECT_EQ("56707c7b3e1092af", span_context_6.idAsHexString());
  EXPECT_EQ(14164264937399213340ULL, span_context_6.parent_id());
  EXPECT_EQ("c49193ea42335d1c", span_context_6.parentIdAsHexString());
  EXPECT_TRUE(span_context_6.annotationSet().cr_);
  EXPECT_TRUE(span_context_6.annotationSet().cs_);
  EXPECT_TRUE(span_context_6.annotationSet().sr_);
  EXPECT_TRUE(span_context_6.annotationSet().ss_);
  EXPECT_EQ("25c6f38dd0600e78;56707c7b3e1092af;c49193ea42335d1c;cr;cs;sr;ss",
            span_context_6.serializeToString());
}
} // namespace Zipkin
} // namespace Envoy
