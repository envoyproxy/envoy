#include <cstdint>
#include <iomanip>
#include <memory>
#include <typeinfo>
#include <utility>
#include <vector>

#include "source/common/tracing/common_values.h"
#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/tracers/datadog/span.h"
#include "source/extensions/tracers/datadog/time_util.h"

#include "test/mocks/tracing/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "datadog/clock.h"
#include "datadog/collector.h"
#include "datadog/event_scheduler.h"
#include "datadog/expected.h"
#include "datadog/http_client.h"
#include "datadog/id_generator.h"
#include "datadog/logger.h"
#include "datadog/null_collector.h"
#include "datadog/sampling_priority.h"
#include "datadog/trace_segment.h"
#include "datadog/tracer.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace datadog {
namespace tracing {

bool operator==(const TimePoint& left, const TimePoint& right) {
  return left.wall == right.wall && left.tick == right.tick;
}

} // namespace tracing
} // namespace datadog

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

// Define a custom Logger for testing
class TestLogger : public datadog::tracing::Logger {
public:
  ~TestLogger() override = default;

  void log_error(const LogFunc& f) override {
    std::ostringstream stream;
    f(stream);
    errors_.push_back(stream.str());
  }

  void log_startup(const LogFunc& f) override {
    std::ostringstream stream;
    f(stream);
    startup_messages_.push_back(stream.str());
  }

  void log_error(const datadog::tracing::Error& error) override {
    errors_.push_back(error.message);
  }

  void log_error(datadog::tracing::StringView message) override {
    errors_.emplace_back(std::string(message.data(), message.size()));
  }

  const std::vector<std::string>& errors() const { return errors_; }
  const std::vector<std::string>& startup_messages() const { return startup_messages_; }

private:
  std::vector<std::string> errors_;
  std::vector<std::string> startup_messages_;
};

// Mock HTTPClient for tests that doesn't actually send requests
class TestHTTPClient : public datadog::tracing::HTTPClient {
public:
  datadog::tracing::Expected<void> post(const URL&, HeadersSetter, std::string, ResponseHandler,
                                        ErrorHandler,
                                        std::chrono::steady_clock::time_point) override {
    // Do nothing - just return success
    return datadog::tracing::nullopt;
  }

  void drain(std::chrono::steady_clock::time_point) override {}

  std::string config() const override { return R"({"type":"TestHTTPClient"})"; }
};

// Mock EventScheduler for tests that doesn't actually schedule events
class TestEventScheduler : public datadog::tracing::EventScheduler {
public:
  datadog::tracing::EventScheduler::Cancel
  schedule_recurring_event(std::chrono::steady_clock::duration, std::function<void()>) override {
    // Return a no-op cancel function
    return []() {};
  }

  std::string config() const override { return R"({"type":"TestEventScheduler"})"; }
};

// Custom ID generator that always returns a fixed value
class FixedIDGenerator : public datadog::tracing::IDGenerator {
  std::uint64_t id_;

public:
  explicit FixedIDGenerator(std::uint64_t id) : id_(id) {}

  std::uint64_t span_id() const override { return id_; }

  datadog::tracing::TraceID trace_id(const datadog::tracing::TimePoint&) const override {
    return datadog::tracing::TraceID{id_};
  }
};

// Test class to verify Datadog tracer behaviors
class DatadogTracerSpanTest : public testing::Test {
public:
  DatadogTracerSpanTest()
      : test_logger_(std::make_shared<TestLogger>()),
        id_generator_(std::make_shared<FixedIDGenerator>(0xcafebabe)), tracer_(createTracer()) {}

  // Creates a Datadog tracer for testing
  datadog::tracing::Tracer createTracer() {
    datadog::tracing::TracerConfig config;
    config.service = "test-service";
    config.logger = test_logger_;
    config.log_on_startup = false;

    // Even though we use NullCollector, the DatadogAgent config still needs to be valid.
    // Provide test implementations of required dependencies.
    config.agent.http_client = std::make_shared<TestHTTPClient>();
    config.agent.event_scheduler = std::make_shared<TestEventScheduler>();

    // Use NullCollector to avoid actually sending traces.
    config.collector = std::make_shared<datadog::tracing::NullCollector>();

    // Disable telemetry to avoid HTTP client issues during tests.
    config.telemetry.enabled = false;

    // Configure a sampler rule that drops all spans.
    datadog::tracing::TraceSamplerConfig::Rule rule;
    rule.sample_rate = 0;
    config.trace_sampler.rules.push_back(std::move(rule));

    auto validated_config = datadog::tracing::finalize_config(config);
    if (!validated_config) {
      ADD_FAILURE() << "finalize_config failed: " << validated_config.error().message;
      // Return a tracer with minimal config to avoid crashing the test.
      // The ADD_FAILURE above will mark the test as failed.
      config.report_traces = false;
      validated_config = datadog::tracing::finalize_config(config);
      EXPECT_TRUE(validated_config);
    }
    return datadog::tracing::Tracer(*validated_config, id_generator_);
  }

protected:
  std::shared_ptr<TestLogger> test_logger_;
  std::shared_ptr<FixedIDGenerator> id_generator_;
  datadog::tracing::Tracer tracer_;
  Event::SimulatedTimeSystem time_;
};

TEST_F(DatadogTracerSpanTest, SetOperation) {
  auto dd_span = tracer_.create_span();
  Span span(std::move(dd_span));

  // Setting the operation name actually sets the resource name, because Envoy's
  // notion of operation name more closely matches Datadog's notion of resource
  // name.
  span.setOperation("gastric bypass");
  span.finishSpan();

  // Verify the span successfully completes without errors
  EXPECT_TRUE(test_logger_->errors().empty());
}

TEST_F(DatadogTracerSpanTest, SetTag) {
  auto dd_span = tracer_.create_span();
  Span span(std::move(dd_span));

  span.setTag("foo", "bar");
  span.setTag("boom", "bam");
  span.setTag("foo", "new"); // Should overwrite previous value
  span.finishSpan();

  // Verify the span successfully completes without errors
  EXPECT_TRUE(test_logger_->errors().empty());
}

TEST_F(DatadogTracerSpanTest, SetTagResourceName) {
  // The "resource.name" tag is special. It doesn't set a tag, but instead sets
  // the span's resource name.
  auto dd_span = tracer_.create_span();
  Span span(std::move(dd_span));

  span.setTag("resource.name", "vespene gas");
  span.finishSpan();

  // Verify the span successfully completes without errors
  EXPECT_TRUE(test_logger_->errors().empty());
}

// The "error" and "error.reason" tags are special.
//
// - The "error" tag is only ever set to "true", and doing so indicates that
//   an error occurred during the extent of the span. The corresponding notion
//   for a Datadog span is to call `.set_error(true)`, and the result is that
//   the underlying Datadog span's `error` property will be `1`.
// - The "error.reason" tag is set to some description of the kind of error
//   that occurred. It's debatable whether this more closely corresponds to
//   Datadog's `.set_error_message(...)` or to `.set_error_type(...)`, but this
//   library chooses `.set_error_message(...)`, which has the result of setting
//   the "error.message" tag. The "error.reason" tag is also set to the same
//   value.
// - Note that calling `.set_error_message(...)` causes `.set_error(true)` to
//   be called. However, it might be possible for Envoy to set the
//   "error.reason" tag without also setting the "error" tag. This library
//   chooses to treat all "error.reason" as if they imply a corresponding
//   "error", i.e. setting "error.reason" without "error" still implies an
//   error.

TEST_F(DatadogTracerSpanTest, SetTagError) {
  auto dd_span = tracer_.create_span();
  Span span(std::move(dd_span));

  const auto& Tags = Envoy::Tracing::Tags::get();
  span.setTag(Tags.Error, Tags.True);
  span.finishSpan();

  // Verify the span successfully completes without errors
  EXPECT_TRUE(test_logger_->errors().empty());
}

TEST_F(DatadogTracerSpanTest, SetTagErrorBogus) {
  auto dd_span = tracer_.create_span();
  Span span(std::move(dd_span));

  const auto& Tags = Envoy::Tracing::Tags::get();
  // `Tags.True`, which is "true", is the only value accepted for the
  // `Tags.Error` ("error") tag. All others are ignored.
  span.setTag(Tags.Error, Tags.True);
  span.setTag(Tags.Error, "false");
  span.setTag(Tags.Error, "supercalifragilisticexpialidocious");
  span.finishSpan();

  // Verify the span successfully completes without errors
  EXPECT_TRUE(test_logger_->errors().empty());
}

TEST_F(DatadogTracerSpanTest, SetTagErrorReason) {
  auto dd_span = tracer_.create_span();
  Span span(std::move(dd_span));

  const auto& Tags = Envoy::Tracing::Tags::get();
  span.setTag(Tags.ErrorReason, "not enough minerals");
  span.finishSpan();

  // Verify the span successfully completes without errors
  EXPECT_TRUE(test_logger_->errors().empty());
}

TEST_F(DatadogTracerSpanTest, InjectContext) {
  auto dd_span = tracer_.create_span();
  Span span(std::move(dd_span));

  Tracing::TestTraceContextImpl context{};
  span.injectContext(context, Tracing::UpstreamContext());

  // Span::injectContext doesn't modify any of the named fields.
  EXPECT_EQ("", context.context_protocol_);
  EXPECT_EQ("", context.context_host_);
  EXPECT_EQ("", context.context_path_);
  EXPECT_EQ("", context.context_method_);

  // Span::injectContext inserts propagation headers that depend on the
  // propagation style configured (i.e. the DD_TRACE_PROPAGATION_STYLE_INJECT
  // environment variable). The default style includes Datadog propagation
  // headers, so we check those here.
  auto found = context.context_map_.find("x-datadog-trace-id");
  ASSERT_NE(context.context_map_.end(), found);
  EXPECT_EQ(std::to_string(0xcafebabe), found->second);
  found = context.context_map_.find("x-datadog-parent-id");
  ASSERT_NE(context.context_map_.end(), found);
  EXPECT_EQ(std::to_string(0xcafebabe), found->second);
  found = context.context_map_.find("x-datadog-sampling-priority");
  ASSERT_NE(context.context_map_.end(), found);
  // USER_DROP because we set a rule that keeps nothing.
  EXPECT_EQ(std::to_string(int(datadog::tracing::SamplingPriority::USER_DROP)), found->second);
}

TEST_F(DatadogTracerSpanTest, SpawnChild) {
  auto dd_span = tracer_.create_span();
  Span parent(std::move(dd_span));

  const auto child_start = time_.timeSystem().systemTime();
  // Setting the operation name actually sets the resource name, because
  // Envoy's notion of operation name more closely matches Datadog's notion of
  // resource name. The actual operation name is hard-coded as "envoy.proxy".
  auto child = parent.spawnChild(Tracing::MockConfig{}, "child", child_start);

  // Make sure the child span is valid
  EXPECT_NE(nullptr, child);

  child->finishSpan();
  parent.finishSpan();

  // Verify the spans successfully complete without errors
  EXPECT_TRUE(test_logger_->errors().empty());
}

TEST_F(DatadogTracerSpanTest, SetSampledTrue) {
  // `Span::setSampled(bool)` on any span causes the entire group (chunk) of
  // spans to take that sampling override. In terms of dd-trace-cpp, this means
  // that the local root of the chunk will have its
  // `datadog::tracing::tags::internal::sampling_priority` tag set to either -1
  // (hard drop) or 2 (hard keep).
  auto dd_span = tracer_.create_span();

  // First ensure that the trace will be dropped (until we override it by
  // calling `setSampled`, below).
  dd_span.trace_segment().override_sampling_priority(
      static_cast<int>(datadog::tracing::SamplingPriority::USER_DROP));

  Span parent(std::move(dd_span));
  auto child = parent.spawnChild(Tracing::MockConfig{}, "child", time_.timeSystem().systemTime());

  child->setSampled(true);
  child->finishSpan();
  parent.finishSpan();

  // Verify the spans successfully complete without errors.
  EXPECT_TRUE(test_logger_->errors().empty());
}

TEST_F(DatadogTracerSpanTest, SetSampledFalse) {
  // `Span::setSampled(bool)` on any span causes the entire group (chunk) of
  // spans to take that sampling override. In terms of dd-trace-cpp, this means
  // that the local root of the chunk will have its
  // `datadog::tracing::tags::internal::sampling_priority` tag set to either -1
  // (hard drop) or 2 (hard keep).
  auto dd_span = tracer_.create_span();

  // First ensure that the trace will be kept (until we override it by
  // calling `setSampled`, below).
  dd_span.trace_segment().override_sampling_priority(
      static_cast<int>(datadog::tracing::SamplingPriority::USER_KEEP));

  Span parent(std::move(dd_span));
  auto child = parent.spawnChild(Tracing::MockConfig{}, "child", time_.timeSystem().systemTime());

  child->setSampled(false);
  child->finishSpan();
  parent.finishSpan();

  // Verify the spans successfully complete without errors.
  EXPECT_TRUE(test_logger_->errors().empty());
}

TEST_F(DatadogTracerSpanTest, UseLocalDecisionDefault) {
  auto dd_span = tracer_.create_span();
  Span span(std::move(dd_span));
  EXPECT_EQ(false, span.useLocalDecision());
}

TEST_F(DatadogTracerSpanTest, UseLocalDecisionTrue) {
  auto dd_span = tracer_.create_span();
  Span span(std::move(dd_span), true);
  EXPECT_EQ(true, span.useLocalDecision());
}

TEST_F(DatadogTracerSpanTest, UseLocalDecisionFalse) {
  auto dd_span = tracer_.create_span();
  Span span(std::move(dd_span), false);
  EXPECT_EQ(false, span.useLocalDecision());
}

TEST_F(DatadogTracerSpanTest, Baggage) {
  // Baggage is not supported by dd-trace-cpp, so `Span::getBaggage` and
  // `Span::setBaggage` do nothing.
  auto dd_span = tracer_.create_span();
  Span span(std::move(dd_span));

  EXPECT_EQ("", span.getBaggage("foo"));
  span.setBaggage("foo", "bar");
  EXPECT_EQ("", span.getBaggage("foo"));
}

TEST_F(DatadogTracerSpanTest, GetTraceId) {
  auto dd_span = tracer_.create_span();
  Span span(std::move(dd_span));

  // We set the ID to 0xcafebabe in our test fixture
  EXPECT_EQ("cafebabe", span.getTraceId());
  EXPECT_EQ("", span.getSpanId());
}

TEST_F(DatadogTracerSpanTest, NoOpMode) {
  // `Span::finishSpan` destroys its `datadog::tracing::Span` member.
  // Subsequently, methods called on the `Span` do nothing.
  //
  // I don't expect that Envoy will call methods on a finished span, and it's
  // hard to verify that the operations are no-ops, so this test just exercises
  // the code paths to verify that they don't trip any memory violations.
  auto dd_span = tracer_.create_span();
  Span span(std::move(dd_span));

  span.finishSpan();

  // `Span::finishSpan` is idempotent.
  span.finishSpan();

  // Inner `datadog::tracing::Span` really is destroyed.
  const datadog::tracing::Optional<datadog::tracing::Span>& impl = span.impl();
  EXPECT_EQ(datadog::tracing::nullopt, impl);

  // Other methods.
  span.setOperation("foo");
  span.setTag("foo", "bar");
  // `Span::log` doesn't do anything in any case.
  span.log(time_.timeSystem().systemTime(), "ignored");
  Tracing::TestTraceContextImpl context{};
  span.injectContext(context, Tracing::UpstreamContext());
  EXPECT_EQ("", context.context_protocol_);
  EXPECT_EQ("", context.context_host_);
  EXPECT_EQ("", context.context_path_);
  EXPECT_EQ("", context.context_method_);
  EXPECT_EQ(0, context.context_map_.size());
  const Tracing::SpanPtr child =
      span.spawnChild(Tracing::MockConfig{}, "child", time_.timeSystem().systemTime());
  EXPECT_NE(nullptr, child);
  const Tracing::Span& child_span = *child;
  EXPECT_EQ(typeid(Tracing::NullSpan), typeid(child_span));
  span.setSampled(true);
  span.setSampled(false);
  EXPECT_EQ("", span.getBaggage("foo"));
  span.setBaggage("foo", "bar");
  EXPECT_EQ("", span.getTraceId());
  EXPECT_EQ("", span.getSpanId());
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
