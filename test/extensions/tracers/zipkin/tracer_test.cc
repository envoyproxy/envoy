#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_impl.h"

#include "extensions/tracers/zipkin/tracer.h"
#include "extensions/tracers/zipkin/util.h"
#include "extensions/tracers/zipkin/zipkin_core_constants.h"

#include "test/mocks/common.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

class TestReporterImpl : public Reporter {
public:
  TestReporterImpl(int value) : value_(value) {}
  void reportSpan(const Span& span) { reported_spans_.push_back(span); }
  int getValue() { return value_; }
  std::vector<Span>& reportedSpans() { return reported_spans_; }

private:
  int value_;
  std::vector<Span> reported_spans_;
};

class ZipkinTracerTest : public testing::Test {
protected:
  Event::SimulatedTimeSystem time_system_;
};

TEST_F(ZipkinTracerTest, spanCreation) {
  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::parseInternetAddressAndPort("127.0.0.1:9000");
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  Tracer tracer("my_service_name", addr, random_generator, false, true, time_system_);
  SystemTime timestamp = time_system_.systemTime();

  NiceMock<Tracing::MockConfig> config;
  ON_CALL(config, operationName()).WillByDefault(Return(Tracing::OperationName::Egress));

  // ==============
  // Test the creation of a root span --> CS
  // ==============
  ON_CALL(random_generator, random()).WillByDefault(Return(1000));
  time_system_.sleep(std::chrono::milliseconds(1));
  SpanPtr root_span = tracer.startSpan(config, "my_span", timestamp);

  EXPECT_EQ("my_span", root_span->name());
  EXPECT_NE(0LL, root_span->startTime());
  EXPECT_NE(0ULL, root_span->traceId());            // trace id must be set
  EXPECT_FALSE(root_span->isSetTraceIdHigh());      // by default, should be using 64 bit trace id
  EXPECT_EQ(root_span->traceId(), root_span->id()); // span id and trace id must be the same
  EXPECT_FALSE(root_span->isSetParentId());         // no parent set
  // span's timestamp must be set
  EXPECT_EQ(
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count(),
      root_span->timestamp());

  // A CS annotation must have been added
  EXPECT_EQ(1ULL, root_span->annotations().size());
  Annotation ann = root_span->annotations()[0];
  EXPECT_EQ(ZipkinCoreConstants::get().CLIENT_SEND, ann.value());
  // annotation's timestamp must be set
  EXPECT_EQ(
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count(),
      ann.timestamp());
  EXPECT_TRUE(ann.isSetEndpoint());
  Endpoint endpoint = ann.endpoint();
  EXPECT_EQ("my_service_name", endpoint.serviceName());

  // The tracer must have been properly set
  EXPECT_EQ(dynamic_cast<TracerInterface*>(&tracer), root_span->tracer());

  // Duration is not set at span-creation time
  EXPECT_FALSE(root_span->isSetDuration());

  // ==============
  // Test the creation of a shared-context span --> SR
  // ==============

  ON_CALL(config, operationName()).WillByDefault(Return(Tracing::OperationName::Ingress));

  SpanContext root_span_context(*root_span);
  SpanPtr server_side_shared_context_span =
      tracer.startSpan(config, "my_span", timestamp, root_span_context);

  EXPECT_NE(0LL, server_side_shared_context_span->startTime());

  EXPECT_EQ("my_span", server_side_shared_context_span->name());

  // trace id must be the same in the CS and SR sides
  EXPECT_EQ(root_span->traceId(), server_side_shared_context_span->traceId());

  // span id must be the same in the CS and SR sides
  EXPECT_EQ(root_span->id(), server_side_shared_context_span->id());

  // The parent should be the same as in the CS side (none in this case)
  EXPECT_FALSE(server_side_shared_context_span->isSetParentId());

  // span timestamp should not be set (it was set in the CS side)
  EXPECT_FALSE(server_side_shared_context_span->isSetTimestamp());

  // An SR annotation must have been added
  EXPECT_EQ(1ULL, server_side_shared_context_span->annotations().size());
  ann = server_side_shared_context_span->annotations()[0];
  EXPECT_EQ(ZipkinCoreConstants::get().SERVER_RECV, ann.value());
  // annotation's timestamp must be set
  EXPECT_EQ(
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count(),
      ann.timestamp());
  EXPECT_TRUE(ann.isSetEndpoint());
  endpoint = ann.endpoint();
  EXPECT_EQ("my_service_name", endpoint.serviceName());

  // The tracer must have been properly set
  EXPECT_EQ(dynamic_cast<TracerInterface*>(&tracer), server_side_shared_context_span->tracer());

  // Duration is not set at span-creation time
  EXPECT_FALSE(server_side_shared_context_span->isSetDuration());

  // ==============
  // Test the creation of a child span --> CS
  // ==============
  ON_CALL(config, operationName()).WillByDefault(Return(Tracing::OperationName::Egress));

  ON_CALL(random_generator, random()).WillByDefault(Return(2000));
  SpanContext server_side_context(*server_side_shared_context_span);
  SpanPtr child_span = tracer.startSpan(config, "my_child_span", timestamp, server_side_context);

  EXPECT_EQ("my_child_span", child_span->name());
  EXPECT_NE(0LL, child_span->startTime());

  // trace id must be retained
  EXPECT_NE(0ULL, child_span->traceId());
  EXPECT_EQ(server_side_shared_context_span->traceId(), child_span->traceId());

  // span id and trace id must NOT be the same
  EXPECT_NE(child_span->traceId(), child_span->id());

  // parent should be the previous span
  EXPECT_TRUE(child_span->isSetParentId());
  EXPECT_EQ(server_side_shared_context_span->id(), child_span->parentId());

  // span's timestamp must be set
  EXPECT_EQ(
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count(),
      child_span->timestamp());

  // A CS annotation must have been added
  EXPECT_EQ(1ULL, child_span->annotations().size());
  ann = child_span->annotations()[0];
  EXPECT_EQ(ZipkinCoreConstants::get().CLIENT_SEND, ann.value());
  // Annotation's timestamp must be set
  EXPECT_EQ(
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count(),
      ann.timestamp());
  EXPECT_TRUE(ann.isSetEndpoint());
  endpoint = ann.endpoint();
  EXPECT_EQ("my_service_name", endpoint.serviceName());

  // The tracer must have been properly set
  EXPECT_EQ(dynamic_cast<TracerInterface*>(&tracer), child_span->tracer());

  // Duration is not set at span-creation time
  EXPECT_FALSE(child_span->isSetDuration());

  // ==============
  // Test the creation of a shared-context span with a parent --> SR
  // ==============

  ON_CALL(config, operationName()).WillByDefault(Return(Tracing::OperationName::Ingress));
  TestRandomGenerator generator;
  const uint generated_parent_id = generator.random();
  SpanContext modified_root_span_context(root_span_context.trace_id_high(),
                                         root_span_context.trace_id(), root_span_context.id(),
                                         generated_parent_id, root_span_context.sampled());
  SpanPtr new_shared_context_span =
      tracer.startSpan(config, "new_shared_context_span", timestamp, modified_root_span_context);
  EXPECT_NE(0LL, new_shared_context_span->startTime());

  EXPECT_EQ("new_shared_context_span", new_shared_context_span->name());

  // trace id must be the same in the CS and SR sides
  EXPECT_EQ(root_span->traceId(), new_shared_context_span->traceId());

  // span id must be the same in the CS and SR sides
  EXPECT_EQ(root_span->id(), new_shared_context_span->id());

  // The parent should be the same as in the CS side
  EXPECT_TRUE(new_shared_context_span->isSetParentId());
  EXPECT_EQ(modified_root_span_context.parent_id(), new_shared_context_span->parentId());

  // span timestamp should not be set (it was set in the CS side)
  EXPECT_FALSE(new_shared_context_span->isSetTimestamp());

  // An SR annotation must have been added
  EXPECT_EQ(1ULL, new_shared_context_span->annotations().size());
  ann = new_shared_context_span->annotations()[0];
  EXPECT_EQ(ZipkinCoreConstants::get().SERVER_RECV, ann.value());
  // annotation's timestamp must be set
  EXPECT_EQ(
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count(),
      ann.timestamp());
  EXPECT_TRUE(ann.isSetEndpoint());
  endpoint = ann.endpoint();
  EXPECT_EQ("my_service_name", endpoint.serviceName());

  // The tracer must have been properly set
  EXPECT_EQ(dynamic_cast<TracerInterface*>(&tracer), new_shared_context_span->tracer());

  // Duration is not set at span-creation time
  EXPECT_FALSE(new_shared_context_span->isSetDuration());
}

TEST_F(ZipkinTracerTest, finishSpan) {
  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::parseInternetAddressAndPort("127.0.0.1:9000");
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  Tracer tracer("my_service_name", addr, random_generator, false, true, time_system_);
  SystemTime timestamp = time_system_.systemTime();

  // ==============
  // Test finishing a span containing a CS annotation
  // ==============

  NiceMock<Tracing::MockConfig> config;
  ON_CALL(config, operationName()).WillByDefault(Return(Tracing::OperationName::Egress));

  // Creates a root-span with a CS annotation
  SpanPtr span = tracer.startSpan(config, "my_span", timestamp);
  span->setSampled(true);

  // Finishing a root span with a CS annotation must add a CR annotation
  span->finish();
  EXPECT_EQ(2ULL, span->annotations().size());

  // Check the CS annotation added at span-creation time
  Annotation ann = span->annotations()[0];
  EXPECT_EQ(ZipkinCoreConstants::get().CLIENT_SEND, ann.value());

  // Annotation's timestamp must be set
  EXPECT_EQ(
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count(),
      ann.timestamp());
  EXPECT_TRUE(ann.isSetEndpoint());
  Endpoint endpoint = ann.endpoint();
  EXPECT_EQ("my_service_name", endpoint.serviceName());

  // Check the CR annotation added when ending the span
  ann = span->annotations()[1];
  EXPECT_EQ(ZipkinCoreConstants::get().CLIENT_RECV, ann.value());
  EXPECT_NE(0ULL, ann.timestamp()); // annotation's timestamp must be set
  EXPECT_TRUE(ann.isSetEndpoint());
  endpoint = ann.endpoint();
  EXPECT_EQ("my_service_name", endpoint.serviceName());

  // ==============
  // Test finishing a span containing an SR annotation
  // ==============

  ON_CALL(config, operationName()).WillByDefault(Return(Tracing::OperationName::Ingress));

  SpanContext context(*span);
  SpanPtr server_side = tracer.startSpan(config, "my_span", timestamp, context);

  // Associate a reporter with the tracer
  TestReporterImpl* reporter_object = new TestReporterImpl(135);
  ReporterPtr reporter_ptr(reporter_object);
  tracer.setReporter(std::move(reporter_ptr));

  // Finishing a server-side span with an SR annotation must add an SS annotation
  server_side->finish();
  EXPECT_EQ(2ULL, server_side->annotations().size());

  // Test if the reporter's reportSpan method was actually called upon finishing the span
  EXPECT_EQ(1ULL, reporter_object->reportedSpans().size());

  // Check the SR annotation added at span-creation time
  ann = server_side->annotations()[0];
  EXPECT_EQ(ZipkinCoreConstants::get().SERVER_RECV, ann.value());
  // Annotation's timestamp must be set
  EXPECT_EQ(
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch()).count(),
      ann.timestamp());
  EXPECT_TRUE(ann.isSetEndpoint());
  endpoint = ann.endpoint();
  EXPECT_EQ("my_service_name", endpoint.serviceName());

  // Check the SS annotation added when ending the span
  ann = server_side->annotations()[1];
  EXPECT_EQ(ZipkinCoreConstants::get().SERVER_SEND, ann.value());
  EXPECT_NE(0ULL, ann.timestamp()); // annotation's timestamp must be set
  EXPECT_TRUE(ann.isSetEndpoint());
  endpoint = ann.endpoint();
  EXPECT_EQ("my_service_name", endpoint.serviceName());
}

TEST_F(ZipkinTracerTest, finishNotSampledSpan) {
  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::parseInternetAddressAndPort("127.0.0.1:9000");
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  Tracer tracer("my_service_name", addr, random_generator, false, true, time_system_);
  SystemTime timestamp = time_system_.systemTime();

  // ==============
  // Test finishing a span that is marked as not sampled
  // ==============

  NiceMock<Tracing::MockConfig> config;
  ON_CALL(config, operationName()).WillByDefault(Return(Tracing::OperationName::Egress));

  // Associate a reporter with the tracer
  TestReporterImpl* reporter_object = new TestReporterImpl(135);
  ReporterPtr reporter_ptr(reporter_object);
  tracer.setReporter(std::move(reporter_ptr));

  // Creates a root-span with a CS annotation
  SpanPtr span = tracer.startSpan(config, "my_span", timestamp);
  span->setSampled(false);
  span->finish();

  // Test if the reporter's reportSpan method was NOT called upon finishing the span
  EXPECT_EQ(0ULL, reporter_object->reportedSpans().size());
}

TEST_F(ZipkinTracerTest, SpanSampledPropagatedToChild) {
  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::parseInternetAddressAndPort("127.0.0.1:9000");
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  Tracer tracer("my_service_name", addr, random_generator, false, true, time_system_);
  SystemTime timestamp = time_system_.systemTime();

  NiceMock<Tracing::MockConfig> config;
  ON_CALL(config, operationName()).WillByDefault(Return(Tracing::OperationName::Egress));

  // Create parent span
  SpanPtr parent_span = tracer.startSpan(config, "parent_span", timestamp);
  parent_span->setSampled(true);

  SpanContext parent_context1(*parent_span);
  SpanPtr child_span1 = tracer.startSpan(config, "child_span 1", timestamp, parent_context1);

  // Test that child span sampled flag is true
  EXPECT_TRUE(child_span1->sampled());

  parent_span->setSampled(false);
  SpanContext parent_context2(*parent_span);
  SpanPtr child_span2 = tracer.startSpan(config, "child_span 2", timestamp, parent_context2);

  // Test that sampled flag is false
  EXPECT_FALSE(child_span2->sampled());
}

TEST_F(ZipkinTracerTest, RootSpan128bitTraceId) {
  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::parseInternetAddressAndPort("127.0.0.1:9000");
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  Tracer tracer("my_service_name", addr, random_generator, true, true, time_system_);
  SystemTime timestamp = time_system_.systemTime();

  NiceMock<Tracing::MockConfig> config;
  ON_CALL(config, operationName()).WillByDefault(Return(Tracing::OperationName::Egress));

  // Create root span
  SpanPtr root_span = tracer.startSpan(config, "root_span", timestamp);

  // Test that high 64 bit trace id is set
  EXPECT_TRUE(root_span->isSetTraceIdHigh());
}

// This test checks that when configured to use shared span context, a child span
// is created with the same id as the parent span.
TEST_F(ZipkinTracerTest, SharedSpanContext) {
  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::parseInternetAddressAndPort("127.0.0.1:9000");
  NiceMock<Runtime::MockRandomGenerator> random_generator;

  const bool shared_span_context = true;
  Tracer tracer("my_service_name", addr, random_generator, false, shared_span_context,
                time_system_);
  const SystemTime timestamp = time_system_.systemTime();

  NiceMock<Tracing::MockConfig> config;
  ON_CALL(config, operationName()).WillByDefault(Return(Tracing::OperationName::Ingress));

  // Create parent span
  SpanPtr parent_span = tracer.startSpan(config, "parent_span", timestamp);
  SpanContext parent_context(*parent_span);

  SpanPtr child_span = tracer.startSpan(config, "child_span", timestamp, parent_context);

  EXPECT_EQ(parent_span->id(), child_span->id());
}

// This test checks that when configured to NOT use shared span context, a child span
// is created with a different id to the parent span.
TEST_F(ZipkinTracerTest, NotSharedSpanContext) {
  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::parseInternetAddressAndPort("127.0.0.1:9000");
  NiceMock<Runtime::MockRandomGenerator> random_generator;

  const bool shared_span_context = false;
  Tracer tracer("my_service_name", addr, random_generator, false, shared_span_context,
                time_system_);
  const SystemTime timestamp = time_system_.systemTime();

  NiceMock<Tracing::MockConfig> config;
  ON_CALL(config, operationName()).WillByDefault(Return(Tracing::OperationName::Ingress));

  // Create parent span
  SpanPtr parent_span = tracer.startSpan(config, "parent_span", timestamp);
  SpanContext parent_context(*parent_span);

  SpanPtr child_span = tracer.startSpan(config, "child_span", timestamp, parent_context);

  EXPECT_EQ(parent_span->id(), child_span->parentId());
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
