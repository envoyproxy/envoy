#include <datadog/clock.h>
#include <datadog/collector.h>
#include <datadog/expected.h>
#include <datadog/id_generator.h>
#include <datadog/logger.h>
#include <datadog/sampling_priority.h>
#include <datadog/span_data.h>
#include <datadog/tags.h>
#include <datadog/tracer.h>

#include <cstdint>
#include <datadog/json.hpp>
#include <iomanip>
#include <memory>
#include <typeinfo>
#include <utility>
#include <vector>

#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/tracers/datadog/span.h"
#include "source/extensions/tracers/datadog/time_util.h"

#include "test/mocks/tracing/mocks.h"
#include "test/test_common/utility.h"

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

struct TestSetup {
  const std::uint64_t id;
  const std::shared_ptr<MockCollector> collector;
  const datadog::tracing::TracerConfig config;
  datadog::tracing::Tracer tracer;
  datadog::tracing::Span span;

  explicit TestSetup(std::uint64_t id = 0xcafebabe)
      : id(id), collector(std::make_shared<MockCollector>()), config(makeConfig(collector)),
        tracer(
            // Override the tracer's ID generator so that all trace IDs and span
            // IDs are `id`.
            *datadog::tracing::finalize_config(config), [id]() { return id; },
            datadog::tracing::default_clock),
        span(tracer.create_span()) {}

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
};

TEST(DatadogTracerSpanTest, SetOperation) {
  TestSetup test;
  Span span{std::move(test.span)};
  span.setOperation("gastric bypass");
  span.finishSpan();

  ASSERT_EQ(1, test.collector->chunks.size());
  const auto& chunk = test.collector->chunks[0];
  ASSERT_EQ(1, chunk.size());
  const auto& data_ptr = chunk[0];
  ASSERT_NE(nullptr, data_ptr);
  const datadog::tracing::SpanData& data = *data_ptr;

  EXPECT_EQ("gastric bypass", data.name);
}

TEST(DatadogTracerSpanTest, SetTag) {
  TestSetup test;
  Span span{std::move(test.span)};
  span.setTag("foo", "bar");
  span.setTag("boom", "bam");
  span.setTag("foo", "new");
  span.finishSpan();

  ASSERT_EQ(1, test.collector->chunks.size());
  const auto& chunk = test.collector->chunks[0];
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
}

TEST(DatadogTracerSpanTest, InjectContext) {
  TestSetup test;
  Span span{std::move(test.span)};

  Tracing::TestTraceContextImpl context{};
  span.injectContext(context, nullptr);
  // Span::injectContext doesn't modify any of named fields.
  EXPECT_EQ("", context.context_protocol_);
  EXPECT_EQ("", context.context_authority_);
  EXPECT_EQ("", context.context_path_);
  EXPECT_EQ("", context.context_method_);

  // Span::injectContext inserts propagation headers that depend on the
  // propagation style configured (i.e. the DD_TRACE_PROPAGATION_STYLE_INJECT
  // environment variable). The default style includes Datadog propagation
  // headers, so we check those here.
  auto found = context.context_map_.find("x-datadog-trace-id");
  ASSERT_NE(context.context_map_.end(), found);
  EXPECT_EQ(std::to_string(test.id), found->second);
  found = context.context_map_.find("x-datadog-parent-id");
  ASSERT_NE(context.context_map_.end(), found);
  EXPECT_EQ(std::to_string(test.id), found->second);
  found = context.context_map_.find("x-datadog-sampling-priority");
  ASSERT_NE(context.context_map_.end(), found);
  // USER_DROP because we set a rule that keeps nothing.
  EXPECT_EQ(std::to_string(int(datadog::tracing::SamplingPriority::USER_DROP)), found->second);
}

TEST(DatadogTracerSpanTest, SpawnChild) {
  TestSetup test;
  const auto child_start = std::chrono::system_clock::now();
  {
    Span parent{std::move(test.span)};
    auto child = parent.spawnChild(Tracing::MockConfig{}, "child", child_start);
    child->finishSpan();
    parent.finishSpan();
  }

  EXPECT_EQ(1, test.collector->chunks.size());
  const auto& spans = test.collector->chunks[0];
  EXPECT_EQ(2, spans.size());
  const auto& child_ptr = spans[1];
  EXPECT_NE(nullptr, child_ptr);
  const datadog::tracing::SpanData& child = *child_ptr;
  EXPECT_EQ(estimateTime(child_start).wall, child.start.wall);
  EXPECT_EQ("child", child.name);
  EXPECT_EQ(test.id, child.trace_id);
  EXPECT_EQ(test.id, child.span_id);
  EXPECT_EQ(test.id, child.parent_id);
}

TEST(DatadogTracerSpanTest, SetSampled) {
  // `Span::setSampled(bool)` on any span causes the entire group (chunk) of
  // spans to take that sampling override. In terms of dd-trace-cpp, this means
  // that the local root of the chunk will have its
  // `datadog::tracing::tags::internal::sampling_priority` tag set to either -1
  // (hard drop) or 2 (hard keep).
  for (bool sampled : {true, false}) {
    TestSetup test;
    {
      Span local_root{std::move(test.span)};
      auto child =
          local_root.spawnChild(Tracing::MockConfig{}, "child", std::chrono::system_clock::now());
      child->setSampled(sampled);
      child->finishSpan();
      local_root.finishSpan();
    }
    EXPECT_EQ(1, test.collector->chunks.size());
    const auto& spans = test.collector->chunks[0];
    EXPECT_EQ(2, spans.size());
    const auto& local_root_ptr = spans[0];
    EXPECT_NE(nullptr, local_root_ptr);
    const datadog::tracing::SpanData& local_root = *local_root_ptr;
    const auto found =
        local_root.numeric_tags.find(datadog::tracing::tags::internal::sampling_priority);
    EXPECT_NE(local_root.numeric_tags.end(), found);
    if (sampled) {
      EXPECT_EQ(2, found->second);
    } else {
      EXPECT_EQ(-1, found->second);
    }
  }
}

TEST(DatadogTracerSpanTest, Baggage) {
  // Baggage is not supported by dd-trace-cpp, so `Span::getBaggage` and
  // `Span::setBaggage` do nothing.
  TestSetup test;
  Span span{std::move(test.span)};
  EXPECT_EQ("", span.getBaggage("foo"));
  span.setBaggage("foo", "bar");
  EXPECT_EQ("", span.getBaggage("foo"));
}

TEST(DatadogTracerSpanTest, GetTraceIdAsHex) {
  TestSetup test{0xcafebabe};
  Span span{std::move(test.span)};
  EXPECT_EQ("cafebabe", span.getTraceIdAsHex());
}

TEST(DatadogTracerSpanTest, NoOpMode) {
  // `Span::finishSpan` destroys its `datadog::tracing::Span` member.
  // Subsequently, methods called on the `Span` do nothing.
  //
  // I don't expect that Envoy will call methods on a finished span, and it's
  // hard to verify that the operations are no-ops, so this test just exercises
  // the code paths to verify that they don't trip any memory violations.
  TestSetup test;
  Span span{std::move(test.span)};
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
  span.log(std::chrono::system_clock::now(), "ignored");
  Tracing::TestTraceContextImpl context{};
  span.injectContext(context, nullptr);
  EXPECT_EQ("", context.context_protocol_);
  EXPECT_EQ("", context.context_authority_);
  EXPECT_EQ("", context.context_path_);
  EXPECT_EQ("", context.context_method_);
  EXPECT_EQ(0, context.context_map_.size());
  const Tracing::SpanPtr child =
      span.spawnChild(Tracing::MockConfig{}, "child", std::chrono::system_clock::now());
  EXPECT_NE(nullptr, child);
  EXPECT_EQ(typeid(Tracing::NullSpan), typeid(*child));
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
