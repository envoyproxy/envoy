#include "zipkin/tracer.h"
#include "zipkin/util.h"
#include "zipkin/zipkin_core_constants.h"

#include "gtest/gtest.h"

namespace Zipkin {

TEST(ZipkinTracerTest, spanCreation) {
  Tracer tracer("my_service_name", "127.0.0.1:9000");
  int64_t timestamp = Util::timeSinceEpochMicro();

  // ==============
  // Test the creation of a root span --> CS
  // ==============

  Span root_span = tracer.startSpan("my_span", timestamp);

  EXPECT_EQ("my_span", root_span.name());
  EXPECT_EQ(timestamp, root_span.startTime());

  EXPECT_NE(0ULL, root_span.traceId());           // trace id must be set
  EXPECT_EQ(root_span.traceId(), root_span.id()); // span id and trace id must be the same
  EXPECT_FALSE(root_span.isSet().parent_id);      // no parent set
  EXPECT_NE(0LL, root_span.timestamp());          // span's timestamp must be set

  // A CS annotation must have been added
  EXPECT_EQ(1ULL, root_span.annotations().size());
  Annotation ann = root_span.annotations()[0];
  EXPECT_EQ(ZipkinCoreConstants::CLIENT_SEND, ann.value());
  EXPECT_NE(0ULL, ann.timestamp()); // annotation's timestamp must be set
  EXPECT_TRUE(ann.isSetEndpoint());
  Endpoint endpoint = ann.endpoint();
  EXPECT_EQ("127.0.0.1", endpoint.ipv4());
  EXPECT_EQ(9000, endpoint.port());
  EXPECT_EQ("my_service_name", endpoint.serviceName());
  EXPECT_FALSE(endpoint.isSetIpv6());

  // The tracer must have been properly set
  EXPECT_EQ(dynamic_cast<TracerPtr>(&tracer), root_span.tracer());

  // Duration is not set at span-creation time
  EXPECT_FALSE(root_span.isSet().duration);

  // ==============
  // Test the creation of a shared-context span --> SR
  // ==============

  SpanContext root_span_context(root_span);
  Span server_side_shared_context_span = tracer.startSpan("my_span", timestamp, root_span_context);

  EXPECT_EQ(timestamp, server_side_shared_context_span.startTime());

  // span name should NOT be set (it was set in the CS side)
  EXPECT_EQ("", server_side_shared_context_span.name());

  // trace id must be the same in the CS and SR sides
  EXPECT_EQ(root_span.traceId(), server_side_shared_context_span.traceId());

  // span id must be the same in the CS and SR sides
  EXPECT_EQ(root_span.id(), server_side_shared_context_span.id());

  // The parent should be the same as in the CS side (none in this case)
  EXPECT_FALSE(server_side_shared_context_span.isSet().parent_id);

  // span timestamp should not be set (it was set in the CS side)
  EXPECT_EQ(0LL, server_side_shared_context_span.timestamp());
  EXPECT_FALSE(server_side_shared_context_span.isSet().timestamp);

  // An SR annotation must have been added
  EXPECT_EQ(1ULL, server_side_shared_context_span.annotations().size());
  ann = server_side_shared_context_span.annotations()[0];
  EXPECT_EQ(ZipkinCoreConstants::SERVER_RECV, ann.value());
  EXPECT_NE(0ULL, ann.timestamp()); // annotation's timestamp must be set
  EXPECT_TRUE(ann.isSetEndpoint());
  endpoint = ann.endpoint();
  EXPECT_EQ("127.0.0.1", endpoint.ipv4());
  EXPECT_EQ(9000, endpoint.port());
  EXPECT_EQ("my_service_name", endpoint.serviceName());
  EXPECT_FALSE(endpoint.isSetIpv6());

  // The tracer must have been properly set
  EXPECT_EQ(dynamic_cast<TracerPtr>(&tracer), server_side_shared_context_span.tracer());

  // Duration is not set at span-creation time
  EXPECT_FALSE(server_side_shared_context_span.isSet().duration);

  // ==============
  // Test the creation of a child span --> CS
  // ==============

  SpanContext server_side_context(server_side_shared_context_span);
  Span child_span = tracer.startSpan("my_child_span", timestamp, server_side_context);

  EXPECT_EQ("my_child_span", child_span.name());
  EXPECT_EQ(timestamp, child_span.startTime());

  // trace id must be retained
  EXPECT_NE(0ULL, child_span.traceId());
  EXPECT_EQ(server_side_shared_context_span.traceId(), child_span.traceId());

  // span id and trace id must NOT be the same
  EXPECT_NE(child_span.traceId(), child_span.id());

  // parent should be the previous span
  EXPECT_TRUE(child_span.isSet().parent_id);
  EXPECT_EQ(server_side_shared_context_span.id(), child_span.parentId());

  // span's timestamp must be set
  EXPECT_NE(0LL, child_span.timestamp());

  // A CS annotation must have been added
  EXPECT_EQ(1ULL, child_span.annotations().size());
  ann = child_span.annotations()[0];
  EXPECT_EQ(ZipkinCoreConstants::CLIENT_SEND, ann.value());
  EXPECT_NE(0ULL, ann.timestamp()); // annotation's timestamp must be set
  EXPECT_TRUE(ann.isSetEndpoint());
  endpoint = ann.endpoint();
  EXPECT_EQ("127.0.0.1", endpoint.ipv4());
  EXPECT_EQ(9000, endpoint.port());
  EXPECT_EQ("my_service_name", endpoint.serviceName());
  EXPECT_FALSE(endpoint.isSetIpv6());

  // The tracer must have been properly set
  EXPECT_EQ(dynamic_cast<TracerPtr>(&tracer), child_span.tracer());

  // Duration is not set at span-creation time
  EXPECT_FALSE(child_span.isSet().duration);
}

TEST(ZipkinTracerTest, finishSpan) {
  Tracer tracer("my_service_name", "127.0.0.1:9000");
  int64_t timestamp = Util::timeSinceEpochMicro();

  // ==============
  // Test finishing a span containing a CS annotation
  // ==============

  // Creates a root-span with a CS annotation
  Span span = tracer.startSpan("my_span", timestamp);

  // Finishing a root span with a CS annotation must add a CR annotation
  span.finish();
  EXPECT_EQ(2ULL, span.annotations().size());

  // Check the CS annotation added at span-creation time
  Annotation ann = span.annotations()[0];
  EXPECT_EQ(ZipkinCoreConstants::CLIENT_SEND, ann.value());
  EXPECT_NE(0ULL, ann.timestamp()); // annotation's timestamp must be set
  EXPECT_TRUE(ann.isSetEndpoint());
  Endpoint endpoint = ann.endpoint();
  EXPECT_EQ("127.0.0.1", endpoint.ipv4());
  EXPECT_EQ(9000, endpoint.port());
  EXPECT_EQ("my_service_name", endpoint.serviceName());
  EXPECT_FALSE(endpoint.isSetIpv6());

  // Check the CR annotation added when ending the span
  ann = span.annotations()[1];
  EXPECT_EQ(ZipkinCoreConstants::CLIENT_RECV, ann.value());
  EXPECT_NE(0ULL, ann.timestamp()); // annotation's timestamp must be set
  EXPECT_TRUE(ann.isSetEndpoint());
  endpoint = ann.endpoint();
  EXPECT_EQ("127.0.0.1", endpoint.ipv4());
  EXPECT_EQ(9000, endpoint.port());
  EXPECT_EQ("my_service_name", endpoint.serviceName());
  EXPECT_FALSE(endpoint.isSetIpv6());
}
} // Zipkin
