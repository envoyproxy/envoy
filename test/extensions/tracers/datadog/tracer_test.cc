#include "envoy/tracing/trace_reason.h"

#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/tracers/datadog/span.h"
#include "source/extensions/tracers/datadog/tracer.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "datadog/error.h"
#include "datadog/expected.h"
#include "datadog/sampling_priority.h"
#include "datadog/trace_segment.h"
#include "datadog/tracer_config.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

class DatadogTracerTest : public testing::Test {
public:
  DatadogTracerTest() {
    cluster_manager_.initializeClusters({"fake_cluster"}, {});
    cluster_manager_.thread_local_cluster_.cluster_.info_->name_ = "fake_cluster";
    cluster_manager_.initializeThreadLocalClusters({"fake_cluster"});
  }

protected:
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Stats::TestUtil::TestStore store_;
  NiceMock<ThreadLocal::MockInstance> thread_local_slot_allocator_;
  Event::SimulatedTimeSystem time_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(DatadogTracerTest, Breathing) {
  // Verify that constructing a `Tracer` instance with mocked dependencies
  // does not throw exceptions.
  datadog::tracing::TracerConfig config;
  config.defaults.service = "envoy";

  Tracer tracer("fake_cluster", "test_host", config, cluster_manager_, *store_.rootScope(),
                thread_local_slot_allocator_);
}

TEST_F(DatadogTracerTest, NoOpMode) {
  // Verify that when the tracer fails to validate its configuration,
  // `startSpan` subsequently returns `NullSpan` instances.
  datadog::tracing::TracerConfig config;
  config.defaults.service = "envoy";
  datadog::tracing::TraceSamplerConfig::Rule invalid_rule;
  // The `sample_rate`, below, is invalid (should be between 0.0 and 1.0).
  // As a result, the constructor of `Tracer` will fail to initialize the
  // underlying `datadog::tracing::Tracer`, and instead go into a no-op mode
  // where `startSpan` returns `NullSpan` instances.
  invalid_rule.sample_rate = -10;
  config.trace_sampler.rules.push_back(invalid_rule);

  Tracer tracer("fake_cluster", "test_host", config, cluster_manager_, *store_.rootScope(),
                thread_local_slot_allocator_);

  Tracing::TestTraceContextImpl context{};
  // Any values will do for the sake of this test.
  Tracing::Decision decision;
  decision.reason = Tracing::Reason::Sampling;
  decision.traced = true;

  const std::string operation_name = "do.thing";
  const SystemTime start = time_.timeSystem().systemTime();
  ON_CALL(stream_info_, startTime()).WillByDefault(testing::Return(start));

  const Tracing::SpanPtr span =
      tracer.startSpan(Tracing::MockConfig{}, context, stream_info_, operation_name, decision);
  ASSERT_TRUE(span);
  const auto as_null_span = dynamic_cast<Tracing::NullSpan*>(span.get());
  EXPECT_NE(nullptr, as_null_span);
}

TEST_F(DatadogTracerTest, SpanProperties) {
  // Verify that span-affecting parameters to `startSpan` are reflected in the
  // resulting span.
  datadog::tracing::TracerConfig config;
  config.defaults.service = "envoy";
  // Configure the tracer to keep all spans. We then override that
  // configuration in the `Tracing::Decision`, below.
  config.trace_sampler.sample_rate = 1.0; // 100%

  Tracer tracer("fake_cluster", "test_host", config, cluster_manager_, *store_.rootScope(),
                thread_local_slot_allocator_);

  Tracing::TestTraceContextImpl context{};
  // A sampling decision of "false" forces the created trace to be dropped,
  // which we will be able to verify by inspecting the span.
  Tracing::Decision decision;
  decision.reason = Tracing::Reason::Sampling;
  decision.traced = false;

  const std::string operation_name = "do.thing";
  const SystemTime start = time_.timeSystem().systemTime();
  ON_CALL(stream_info_, startTime()).WillByDefault(testing::Return(start));

  const Tracing::SpanPtr span =
      tracer.startSpan(Tracing::MockConfig{}, context, stream_info_, operation_name, decision);
  ASSERT_TRUE(span);
  const auto as_dd_span_wrapper = dynamic_cast<Span*>(span.get());
  EXPECT_NE(nullptr, as_dd_span_wrapper);

  const datadog::tracing::Optional<datadog::tracing::Span>& maybe_dd_span =
      as_dd_span_wrapper->impl();
  ASSERT_TRUE(maybe_dd_span);
  const datadog::tracing::Span& dd_span = *maybe_dd_span;

  // Verify that the span has the expected service name, operation name,
  // resource name, start time, and sampling decision.
  // Note that the `operation_name` we specified above becomes the
  // `resource_name()` of the resulting Datadog span, while the Datadog span's
  // `name()` (operation name) is hard-coded to "envoy.proxy."
  EXPECT_EQ("envoy.proxy", dd_span.name());
  EXPECT_EQ("do.thing", dd_span.resource_name());
  EXPECT_EQ("envoy", dd_span.service_name());
  ASSERT_TRUE(dd_span.trace_segment().sampling_decision());
  EXPECT_EQ(int(datadog::tracing::SamplingPriority::USER_DROP),
            dd_span.trace_segment().sampling_decision()->priority);
  EXPECT_EQ(start, dd_span.start_time().wall);
}

TEST_F(DatadogTracerTest, ExtractionSuccess) {
  // Verify that if there is trace information to extract from the
  // `TraceContext` supplied to `startSpan`, that the resulting span is part of
  // the extracted trace.
  datadog::tracing::TracerConfig config;
  config.defaults.service = "envoy";

  Tracer tracer("fake_cluster", "test_host", config, cluster_manager_, *store_.rootScope(),
                thread_local_slot_allocator_);

  // Any values will do for the sake of this test.
  Tracing::Decision decision;
  decision.reason = Tracing::Reason::Sampling;
  decision.traced = true;

  const std::string operation_name = "do.thing";
  const SystemTime start = time_.timeSystem().systemTime();
  ON_CALL(stream_info_, startTime()).WillByDefault(testing::Return(start));

  // trace context in the Datadog style
  Tracing::TestTraceContextImpl context{{"x-datadog-trace-id", "1234"},
                                        {"x-datadog-parent-id", "5678"}};

  const Tracing::SpanPtr span =
      tracer.startSpan(Tracing::MockConfig{}, context, stream_info_, operation_name, decision);
  ASSERT_TRUE(span);
  const auto as_dd_span_wrapper = dynamic_cast<Span*>(span.get());
  EXPECT_NE(nullptr, as_dd_span_wrapper);

  const datadog::tracing::Optional<datadog::tracing::Span>& maybe_dd_span =
      as_dd_span_wrapper->impl();
  ASSERT_TRUE(maybe_dd_span);
  const datadog::tracing::Span& dd_span = *maybe_dd_span;

  EXPECT_EQ(1234, dd_span.trace_id().low);
  ASSERT_TRUE(dd_span.parent_id());
  EXPECT_EQ(5678, *dd_span.parent_id());
}

TEST_F(DatadogTracerTest, ExtractionFailure) {
  // Verify that if there is invalid trace information in the `TraceContext`
  // supplied to `startSpan`, that the resulting span is nonetheless valid (it
  // will be the start of a new trace).
  datadog::tracing::TracerConfig config;
  config.defaults.service = "envoy";

  Tracer tracer("fake_cluster", "test_host", config, cluster_manager_, *store_.rootScope(),
                thread_local_slot_allocator_);

  // Any values will do for the sake of this test.
  Tracing::Decision decision;
  decision.reason = Tracing::Reason::Sampling;
  decision.traced = true;

  const std::string operation_name = "do.thing";
  const SystemTime start = time_.timeSystem().systemTime();
  ON_CALL(stream_info_, startTime()).WillByDefault(testing::Return(start));

  // invalid trace context in the Datadog style
  Tracing::TestTraceContextImpl context{{"x-datadog-trace-id", "nope"},
                                        {"x-datadog-parent-id", "nice try"}};

  const Tracing::SpanPtr span =
      tracer.startSpan(Tracing::MockConfig{}, context, stream_info_, operation_name, decision);
  ASSERT_TRUE(span);
  const auto as_dd_span_wrapper = dynamic_cast<Span*>(span.get());
  EXPECT_NE(nullptr, as_dd_span_wrapper);

  const datadog::tracing::Optional<datadog::tracing::Span>& maybe_dd_span =
      as_dd_span_wrapper->impl();
  ASSERT_TRUE(maybe_dd_span);
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
