#include <datadog/clock.h>
#include <datadog/collector.h>
#include <datadog/expected.h>
#include <datadog/id_generator.h>
#include <datadog/logger.h>
#include <datadog/span_data.h>
#include <datadog/tracer.h>

#include <cstdint>
#include <datadog/json.hpp>
#include <memory>
#include <utility>
#include <vector>

#include "source/extensions/tracers/datadog/span.h"

#include "gtest/gtest.h"

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
  const std::uint64_t span_id;
  const std::shared_ptr<MockCollector> collector;
  const datadog::tracing::TracerConfig config;
  datadog::tracing::Tracer tracer;
  datadog::tracing::Span span;

  explicit TestSetup(std::uint64_t span_id = 0xcafebabe)
      : span_id(span_id), collector(std::make_shared<MockCollector>()),
        config(makeConfig(collector)),
        tracer(
            *datadog::tracing::finalize_config(config), [span_id]() { return span_id; },
            datadog::tracing::default_clock),
        span(tracer.create_span()) {}

private:
  static datadog::tracing::TracerConfig
  makeConfig(const std::shared_ptr<datadog::tracing::Collector>& collector) {
    datadog::tracing::TracerConfig config;
    config.defaults.service = "testsvc";
    config.collector = collector;
    config.logger = std::make_shared<NullLogger>();
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
  // TODO
  ASSERT_TRUE(false);
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
