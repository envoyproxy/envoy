#include "common/tracing/opentracing_driver_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/tracing/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentracing/mocktracer/in_memory_recorder.h"
#include "opentracing/mocktracer/tracer.h"

using testing::Test;

namespace Envoy {
namespace Tracing {

class TestDriver : public OpenTracingDriver {
public:
  explicit TestDriver(Stats::Store& stats) : OpenTracingDriver{stats} {
    opentracing::mocktracer::MockTracerOptions options;
    auto recorder = new opentracing::mocktracer::InMemoryRecorder{};
    recorder_ = recorder;
    options.recorder.reset(recorder);
    tracer_.reset(new opentracing::mocktracer::MockTracer{std::move(options)});
  }

  opentracing::Tracer& tracer() override { return *tracer_; }

  const opentracing::mocktracer::InMemoryRecorder& recorder() const { return *recorder_; }

private:
  const opentracing::mocktracer::InMemoryRecorder* recorder_;
  std::shared_ptr<opentracing::mocktracer::MockTracer> tracer_;
};

class OpenTracingDriverTest : public Test {
public:
  void setupValidDriver() { driver_.reset(new TestDriver{stats_}); }

  const std::string operation_name_{"test"};
  Http::TestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  const Http::TestHeaderMapImpl response_headers_{{":status", "500"}};
  SystemTime start_time_;

  std::unique_ptr<TestDriver> driver_;
  Stats::IsolatedStoreImpl stats_;

  NiceMock<Tracing::MockConfig> config_;
};

TEST_F(OpenTracingDriverTest, FlushSpanWithTag) {
  setupValidDriver();

  SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_, start_time_);
  first_span->setTag("abc", "123");
  first_span->finishSpan();

  const std::unordered_map<std::string, opentracing::Value> expected_tags = {
      {"abc", std::string{"123"}}};

  EXPECT_EQ(1, driver_->recorder().spans().size());
  EXPECT_EQ(expected_tags, driver_->recorder().top().tags);
}
} // namespace Tracing
} // namespace Envoy
