#include <cstdint>
#include <iomanip>
#include <memory>
#include <typeinfo>
#include <utility>
#include <vector>

#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/tracers/datadog/span.h"
#include "source/extensions/tracers/datadog/time_util.h"

#include "test/mocks/tracing/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "datadog/clock.h"
#include "datadog/collector.h"
#include "datadog/expected.h"
#include "datadog/id_generator.h"
#include "datadog/json.hpp"
#include "datadog/logger.h"
#include "datadog/sampling_priority.h"
#include "datadog/span_data.h"
#include "datadog/tags.h"
#include "datadog/trace_segment.h"
#include "datadog/tracer.h"
#include "gtest/gtest.h"

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

class NullLogger : public datadog::tracing::Logger {
public:
  ~NullLogger() override = default;

  void log_error(const LogFunc&) override {}
  void log_startup(const LogFunc&) override {}

  void log_error(const datadog::tracing::Error&) override {}
  void log_error(datadog::tracing::StringView) override {}
};

struct MockCollector : public datadog::tracing::Collector {
  datadog::tracing::Expected<void>
  send(std::vector<std::unique_ptr<datadog::tracing::SpanData>>&& spans,
       const std::shared_ptr<datadog::tracing::TraceSampler>&) override {
    chunks.push_back(std::move(spans));
    return {};
  }

  nlohmann::json config_json() const override {
    return nlohmann::json::object({{"type", "Envoy::Extensions::Tracers::Datadog::MockCollector"}});
  }

  ~MockCollector() override = default;

  std::vector<std::vector<std::unique_ptr<datadog::tracing::SpanData>>> chunks;
};

class MockIDGenerator : public datadog::tracing::IDGenerator {
  std::uint64_t id_;

public:
  explicit MockIDGenerator(std::uint64_t id) : id_(id) {}

  std::uint64_t span_id() const override { return id_; }

  datadog::tracing::TraceID trace_id() const override { return datadog::tracing::TraceID{id_}; }
};

class DatadogTracerSpanTest : public testing::Test {
public:
  DatadogTracerSpanTest()
      : collector_(std::make_shared<MockCollector>()), config_(makeConfig(collector_)),
        tracer_(
            // Override the tracer's ID generator so that all trace IDs and span
            // IDs are 0xcafebabe.
            *datadog::tracing::finalize_config(config_), std::make_shared<MockIDGenerator>(id_),
            datadog::tracing::default_clock),
        span_(tracer_.create_span()) {}

private:
  static datadog::tracing::TracerConfig
  makeConfig(const std::shared_ptr<datadog::tracing::Collector>& collector) {
    datadog::tracing::TracerConfig config;
    config.defaults.service = "testsvc";
    config.collector = collector;
    config.logger = std::make_shared<NullLogger>();
    // Drop all spans. Equivalently, we could keep all spans.
    datadog::tracing::TraceSamplerConfig::Rule rule;
    rule.sample_rate = 0;
    config.trace_sampler.rules.push_back(std::move(rule));
    return config;
  }

protected:
  const std::uint64_t id_{0xcafebabe};
  const std::shared_ptr<MockCollector> collector_;
  const datadog::tracing::TracerConfig config_;
  datadog::tracing::Tracer tracer_;
  datadog::tracing::Span span_;
  Event::SimulatedTimeSystem time_;
};

TEST_F(DatadogTracerSpanTest, SetOperation) {
  Span span{std::move(span_)};
  span.setOperation("gastric bypass");
  span.finishSpan();

  ASSERT_EQ(1, collector_->chunks.size());
  const auto& chunk = collector_->chunks[0];
  ASSERT_EQ(1, chunk.size());
  const auto& data_ptr = chunk[0];
  ASSERT_NE(nullptr, data_ptr);
  const datadog::tracing::SpanData& data = *data_ptr;

  // Setting the operation name actually sets the resource name, because Envoy's
  // notion of operation name more closely matches Datadog's notion of resource
  // name.
  EXPECT_EQ("gastric bypass", data.resource);
}

TEST_F(DatadogTracerSpanTest, SetTag) {
  Span span{std::move(span_)};
  span.setTag("foo", "bar");
  span.setTag("boom", "bam");
  span.setTag("foo", "new");
  span.setTag("resource.name", "vespene gas");
  span.finishSpan();

  ASSERT_EQ(1, collector_->chunks.size());
  const auto& chunk = collector_->chunks[0];
  ASSERT_EQ(1, chunk.size());
  const auto& data_ptr = chunk[0];
  ASSERT_NE(nullptr, data_ptr);
  const datadog::tracing::SpanData& data = *data_ptr;

  auto found = data.tags.find("foo");
  ASSERT_NE(data.tags.end(), found);
  EXPECT_EQ("new", found->second);

  found = data.tags.find("boom");
  ASSERT_NE(data.tags.end(), found);
  EXPECT_EQ("bam", found->second);

  // The "resource.name" tag is special. It doesn't set a tag, but instead the
  // span's resource name.
  found = data.tags.find("resource.name");
  ASSERT_EQ(data.tags.end(), found);
  EXPECT_EQ("vespene gas", data.resource);
}

TEST_F(DatadogTracerSpanTest, InjectContext) {
  Span span{std::move(span_)};

  Tracing::TestTraceContextImpl context{};
  span.injectContext(context, nullptr);
  // Span::injectContext doesn't modify any of named fields.
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
  EXPECT_EQ(std::to_string(id_), found->second);
  found = context.context_map_.find("x-datadog-parent-id");
  ASSERT_NE(context.context_map_.end(), found);
  EXPECT_EQ(std::to_string(id_), found->second);
  found = context.context_map_.find("x-datadog-sampling-priority");
  ASSERT_NE(context.context_map_.end(), found);
  // USER_DROP because we set a rule that keeps nothing.
  EXPECT_EQ(std::to_string(int(datadog::tracing::SamplingPriority::USER_DROP)), found->second);
}

TEST_F(DatadogTracerSpanTest, SpawnChild) {
  const auto child_start = time_.timeSystem().systemTime();
  {
    Span parent{std::move(span_)};
    auto child = parent.spawnChild(Tracing::MockConfig{}, "child", child_start);
    child->finishSpan();
    parent.finishSpan();
  }

  EXPECT_EQ(1, collector_->chunks.size());
  const auto& spans = collector_->chunks[0];
  EXPECT_EQ(2, spans.size());
  const auto& child_ptr = spans[1];
  EXPECT_NE(nullptr, child_ptr);
  const datadog::tracing::SpanData& child = *child_ptr;
  EXPECT_EQ(estimateTime(child_start).wall, child.start.wall);
  EXPECT_EQ("child", child.name);
  EXPECT_EQ(id_, child.trace_id);
  EXPECT_EQ(id_, child.span_id);
  EXPECT_EQ(id_, child.parent_id);
}

TEST_F(DatadogTracerSpanTest, SetSampledTrue) {
  // `Span::setSampled(bool)` on any span causes the entire group (chunk) of
  // spans to take that sampling override. In terms of dd-trace-cpp, this means
  // that the local root of the chunk will have its
  // `datadog::tracing::tags::internal::sampling_priority` tag set to either -1
  // (hard drop) or 2 (hard keep).
  {
    // First ensure that the trace will be dropped (until we override it by
    // calling `setSampled`, below).
    span_.trace_segment().override_sampling_priority(
        static_cast<int>(datadog::tracing::SamplingPriority::USER_DROP));

    Span local_root{std::move(span_)};
    auto child =
        local_root.spawnChild(Tracing::MockConfig{}, "child", time_.timeSystem().systemTime());
    child->setSampled(true);
    child->finishSpan();
    local_root.finishSpan();
  }
  EXPECT_EQ(1, collector_->chunks.size());
  const auto& spans = collector_->chunks[0];
  EXPECT_EQ(2, spans.size());
  const auto& local_root_ptr = spans[0];
  EXPECT_NE(nullptr, local_root_ptr);
  const datadog::tracing::SpanData& local_root = *local_root_ptr;
  const auto found =
      local_root.numeric_tags.find(datadog::tracing::tags::internal::sampling_priority);
  EXPECT_NE(local_root.numeric_tags.end(), found);
  EXPECT_EQ(2, found->second);
}

TEST_F(DatadogTracerSpanTest, SetSampledFalse) {
  // `Span::setSampled(bool)` on any span causes the entire group (chunk) of
  // spans to take that sampling override. In terms of dd-trace-cpp, this means
  // that the local root of the chunk will have its
  // `datadog::tracing::tags::internal::sampling_priority` tag set to either -1
  // (hard drop) or 2 (hard keep).
  {
    // First ensure that the trace will be kept (until we override it by calling
    // `setSampled`, below).
    span_.trace_segment().override_sampling_priority(
        static_cast<int>(datadog::tracing::SamplingPriority::USER_KEEP));

    Span local_root{std::move(span_)};
    auto child =
        local_root.spawnChild(Tracing::MockConfig{}, "child", time_.timeSystem().systemTime());
    child->setSampled(false);
    child->finishSpan();
    local_root.finishSpan();
  }
  EXPECT_EQ(1, collector_->chunks.size());
  const auto& spans = collector_->chunks[0];
  EXPECT_EQ(2, spans.size());
  const auto& local_root_ptr = spans[0];
  EXPECT_NE(nullptr, local_root_ptr);
  const datadog::tracing::SpanData& local_root = *local_root_ptr;
  const auto found =
      local_root.numeric_tags.find(datadog::tracing::tags::internal::sampling_priority);
  EXPECT_NE(local_root.numeric_tags.end(), found);
  EXPECT_EQ(-1, found->second);
}

TEST_F(DatadogTracerSpanTest, Baggage) {
  // Baggage is not supported by dd-trace-cpp, so `Span::getBaggage` and
  // `Span::setBaggage` do nothing.
  Span span{std::move(span_)};
  EXPECT_EQ("", span.getBaggage("foo"));
  span.setBaggage("foo", "bar");
  EXPECT_EQ("", span.getBaggage("foo"));
}

TEST_F(DatadogTracerSpanTest, GetTraceIdAsHex) {
  Span span{std::move(span_)};
  EXPECT_EQ("cafebabe", span.getTraceIdAsHex());
}

TEST_F(DatadogTracerSpanTest, NoOpMode) {
  // `Span::finishSpan` destroys its `datadog::tracing::Span` member.
  // Subsequently, methods called on the `Span` do nothing.
  //
  // I don't expect that Envoy will call methods on a finished span, and it's
  // hard to verify that the operations are no-ops, so this test just exercises
  // the code paths to verify that they don't trip any memory violations.
  Span span{std::move(span_)};
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
  span.injectContext(context, nullptr);
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
  EXPECT_EQ("", span.getTraceIdAsHex());
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
