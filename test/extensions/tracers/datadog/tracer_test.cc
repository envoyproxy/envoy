#include <cstdlib>

#include "envoy/tracing/trace_reason.h"

#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/tracers/datadog/span.h"
#include "source/extensions/tracers/datadog/tracer.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "datadog/error.h"
#include "datadog/expected.h"
#include "datadog/optional.h"
#include "datadog/propagation_style.h"
#include "datadog/sampling_priority.h"
#include "datadog/trace_segment.h"
#include "datadog/tracer_config.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

class EnvVarGuard {
public:
  EnvVarGuard(const std::string& name, const std::string& value) : name_(name) {
    if (const char* const previous = std::getenv(name.c_str())) {
      previous_value_ = previous;
    }
    const int overwrite = 1; // Yes, overwrite it.
    TestEnvironment::setEnvVar(name, value, overwrite);
  }

  ~EnvVarGuard() {
    if (previous_value_) {
      const int overwrite = 1; // Yes, overwrite it.
      TestEnvironment::setEnvVar(name_, *previous_value_, overwrite);
    } else {
      TestEnvironment::unsetEnvVar(name_);
    }
  }

private:
  std::string name_;
  datadog::tracing::Optional<std::string> previous_value_;
};

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

TEST_F(DatadogTracerTest, EnvoySamplingVersusExtractedSampling) {
  // Verify that sampling decisions extracted from incoming requests are honored
  // regardless of the sampling decision made by Envoy (i.e. `bool
  // Tracing::Decision::traced`).
  //
  // We test two styles of extraction: OpenTelemetry's W3C "tracecontext" style
  // and Datadog's "datadog" style. When trace context is extracted in either of
  // these styles, a sampling decision might be present. If a sampling decision
  // is present, then the resulting sampling priority in the extracted trace
  // must be the same as that which was extracted.
  //
  // If a sampling decision is not present in the extracted trace context, then
  // an Envoy decision of "drop" is honored. An Envoy decision of "keep"
  // delegates the sampling decision to the underlying Datadog tracer, which
  // will not make a sampling decision immediately.

  struct Case {
    int line;
    datadog::tracing::Optional<int> extracted_sampling_priority;
    bool envoy_decision_keep;
    datadog::tracing::PropagationStyle extraction_style;
    // `resulting_sampling_priority` is the sampling priority that results from
    // trace context extraction.
    // It's not necessarily the sampling priority that would be sent to the
    // Datadog Agent.
    // If `resulting_sampling_priority` is null, then that means that the tracer
    // does not make an initial sampling decision, though it will make one by
    // the time is sends spans to the Datadog Agent or injects trace context
    // into an outgoing request.
    datadog::tracing::Optional<int> resulting_sampling_priority;
  } cases[] = {
      {__LINE__, datadog::tracing::nullopt, true, datadog::tracing::PropagationStyle::DATADOG,
       datadog::tracing::nullopt},
      // Note that the `resulting_sampling_priority` in this case is an artifact
      // of "traceparent" always containing a sampling decision in its flags. See
      // the main body of the test, below, for more information.
      {__LINE__, datadog::tracing::nullopt, true, datadog::tracing::PropagationStyle::W3C, 0},
      // This is the only case, at least in this test, where Envoy's decision
      // affects the resulting sampling priority.
      {__LINE__, datadog::tracing::nullopt, false, datadog::tracing::PropagationStyle::DATADOG, -1},
      {__LINE__, datadog::tracing::nullopt, false, datadog::tracing::PropagationStyle::W3C, 0},

      {__LINE__, -1, true, datadog::tracing::PropagationStyle::DATADOG, -1},
      {__LINE__, -1, true, datadog::tracing::PropagationStyle::W3C, -1},
      {__LINE__, -1, false, datadog::tracing::PropagationStyle::DATADOG, -1},
      {__LINE__, -1, false, datadog::tracing::PropagationStyle::W3C, -1},

      {__LINE__, 0, true, datadog::tracing::PropagationStyle::DATADOG, 0},
      {__LINE__, 0, true, datadog::tracing::PropagationStyle::W3C, 0},
      {__LINE__, 0, false, datadog::tracing::PropagationStyle::DATADOG, 0},
      {__LINE__, 0, false, datadog::tracing::PropagationStyle::W3C, 0},

      {__LINE__, 1, true, datadog::tracing::PropagationStyle::DATADOG, 1},
      {__LINE__, 1, true, datadog::tracing::PropagationStyle::W3C, 1},
      {__LINE__, 1, false, datadog::tracing::PropagationStyle::DATADOG, 1},
      {__LINE__, 1, false, datadog::tracing::PropagationStyle::W3C, 1},

      {__LINE__, 2, true, datadog::tracing::PropagationStyle::DATADOG, 2},
      {__LINE__, 2, true, datadog::tracing::PropagationStyle::W3C, 2},
      {__LINE__, 2, false, datadog::tracing::PropagationStyle::DATADOG, 2},
      {__LINE__, 2, false, datadog::tracing::PropagationStyle::W3C, 2},
  };

  for (const Case& test_case : cases) {
    std::ostringstream failure_context;
    failure_context << "Failure occurred for test entry on line " << test_case.line;

    std::string style_name;
    if (test_case.extraction_style == datadog::tracing::PropagationStyle::DATADOG) {
      style_name = "datadog";
    } else {
      ASSERT_EQ(test_case.extraction_style, datadog::tracing::PropagationStyle::W3C)
          << failure_context.str();
      style_name = "tracecontext";
    }

    EnvVarGuard guard{"DD_TRACE_PROPAGATION_STYLE", style_name};
    datadog::tracing::TracerConfig config;
    config.defaults.service = "envoy";
    Tracer tracer("fake_cluster", "test_host", config, cluster_manager_, *store_.rootScope(),
                  thread_local_slot_allocator_);

    Tracing::Decision envoy_decision;
    envoy_decision.reason = Tracing::Reason::Sampling;
    envoy_decision.traced = test_case.envoy_decision_keep;

    const std::string operation_name = "do.thing";

    Tracing::TestTraceContextImpl context{{}};
    if (test_case.extraction_style == datadog::tracing::PropagationStyle::DATADOG) {
      context.context_map_["x-datadog-trace-id"] = "123";
      context.context_map_["x-datadog-parent-id"] = "456";
      if (test_case.extracted_sampling_priority) {
        context.context_map_["x-datadog-sampling-priority"] =
            std::to_string(*test_case.extracted_sampling_priority);
      }
    } else {
      ASSERT_EQ(test_case.extraction_style, datadog::tracing::PropagationStyle::W3C)
          << failure_context.str();
      std::string flags;
      if (test_case.extracted_sampling_priority) {
        const int priority = *test_case.extracted_sampling_priority;
        flags = priority <= 0 ? "00" : "01";
        context.context_map_["tracestate"] = "dd=s:" + std::to_string(priority);
      } else {
        // There's no such thing as the absence of a sampling decision with
        // "traceparent," so default to "drop."
        flags = "00";
      }
      context.context_map_["traceparent"] =
          "00-0000000000000000000000000000007b-00000000000001c8-" + flags;
    }

    const Tracing::SpanPtr span = tracer.startSpan(Tracing::MockConfig{}, context, stream_info_,
                                                   operation_name, envoy_decision);
    ASSERT_TRUE(span) << failure_context.str();
    const auto as_dd_span_wrapper = dynamic_cast<Span*>(span.get());
    EXPECT_NE(nullptr, as_dd_span_wrapper) << failure_context.str();

    const datadog::tracing::Optional<datadog::tracing::Span>& maybe_dd_span =
        as_dd_span_wrapper->impl();
    ASSERT_TRUE(maybe_dd_span) << failure_context.str();
    const datadog::tracing::Span& dd_span = *maybe_dd_span;

    const datadog::tracing::Optional<datadog::tracing::SamplingDecision> decision =
        dd_span.trace_segment().sampling_decision();
    if (test_case.resulting_sampling_priority) {
      // We expect that the tracer made a sampling decision immediately, and
      // that it has the expected sampling priority.
      ASSERT_NE(datadog::tracing::nullopt, decision) << failure_context.str();
      EXPECT_EQ(*test_case.resulting_sampling_priority, decision->priority)
          << failure_context.str();
    } else {
      // We expect that the tracer did not immediately make a sampling decision.
      EXPECT_EQ(datadog::tracing::nullopt, decision) << failure_context.str();
    }
  }
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
