#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_impl.h"

#include "extensions/tracers/xray/sampling.h"
#include "extensions/tracers/xray/tracer.h"
#include "extensions/tracers/xray/util.h"
#include "extensions/tracers/xray/xray_core_constants.h"

#include "test/mocks/common.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {
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

class XRayTracerTest : public testing::Test {
protected:
  Event::SimulatedTimeSystem time_system_;
};

TEST_F(XRayTracerTest, spanCreation) {
  NiceMock<Runtime::MockRandomGenerator> random_generator;
  LocalizedSamplingStrategy localized_sampling_strategy =
      LocalizedSamplingStrategy("", time_system_);
  Tracer tracer("test_service_name", random_generator, localized_sampling_strategy, time_system_);
  SystemTime timestamp = time_system_.systemTime();

  NiceMock<Tracing::MockConfig> config;
  ON_CALL(config, operationName()).WillByDefault(Return(Tracing::OperationName::Egress));

  // ==============
  // Test the creation of a root segment
  // ==============
  ON_CALL(random_generator, random()).WillByDefault(Return(1000));
  time_system_.sleep(std::chrono::milliseconds(1));
  SpanPtr root_span = tracer.startSpan(config, "test_segment1", timestamp);

  EXPECT_EQ("test_segment1", root_span->name());
  EXPECT_NE(0LL, root_span->startTime());
  EXPECT_NE("", root_span->traceId());      // trace id must be set
  EXPECT_FALSE(root_span->isSetParentId()); // no parent set
  // segment and subsegment have the same name
  EXPECT_EQ("test_segment1", root_span->childSpans()[0].name());
  EXPECT_NE(0LL, root_span->childSpans()[0].startTime());

  // ==============
  // Test the creation of a shared-context segment
  // ==============
  uint64_t span_id(0);
  ON_CALL(config, operationName()).WillByDefault(Return(Tracing::OperationName::Ingress));
  SpanContext root_span_context(root_span->traceId(), span_id, root_span->childSpans()[0].id(),
                                true);
  SpanPtr upstream_shared_context_span =
      tracer.startSpan(config, "test_segment2", timestamp, root_span_context);

  EXPECT_NE(0LL, upstream_shared_context_span->startTime());
  EXPECT_EQ("test_segment2", upstream_shared_context_span->name());
  // trace id must be the same in downstream segment
  EXPECT_EQ(root_span->traceId(), upstream_shared_context_span->traceId());
  // segment and subsegment have the same name
  EXPECT_EQ("test_segment2", upstream_shared_context_span->childSpans()[0].name());
  EXPECT_NE(0LL, upstream_shared_context_span->childSpans()[0].startTime());
  EXPECT_EQ(root_span->childSpans()[0].id(), upstream_shared_context_span->parentId());
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
