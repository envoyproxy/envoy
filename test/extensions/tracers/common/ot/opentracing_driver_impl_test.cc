#include "extensions/tracers/common/ot/opentracing_driver_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/tracing/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentracing/mocktracer/in_memory_recorder.h"
#include "opentracing/mocktracer/tracer.h"

using testing::Test;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Common {
namespace Ot {

class TestDriver : public OpenTracingDriver {
public:
  TestDriver(OpenTracingDriver::PropagationMode propagation_mode,
             const opentracing::mocktracer::PropagationOptions& propagation_options,
             Stats::Store& stats)
      : OpenTracingDriver{stats}, propagation_mode_{propagation_mode} {
    opentracing::mocktracer::MockTracerOptions options;
    auto recorder = new opentracing::mocktracer::InMemoryRecorder{};
    recorder_ = recorder;
    options.recorder.reset(recorder);
    options.propagation_options = propagation_options;
    tracer_.reset(new opentracing::mocktracer::MockTracer{std::move(options)});
  }

  const opentracing::mocktracer::InMemoryRecorder& recorder() const { return *recorder_; }

  // OpenTracingDriver
  opentracing::Tracer& tracer() override { return *tracer_; }

  PropagationMode propagationMode() const override { return propagation_mode_; }

private:
  const OpenTracingDriver::PropagationMode propagation_mode_;
  const opentracing::mocktracer::InMemoryRecorder* recorder_;
  std::shared_ptr<opentracing::mocktracer::MockTracer> tracer_;
};

class OpenTracingDriverTest : public Test {
public:
  void
  setupValidDriver(OpenTracingDriver::PropagationMode propagation_mode =
                       OpenTracingDriver::PropagationMode::SingleHeader,
                   const opentracing::mocktracer::PropagationOptions& propagation_options = {}) {
    driver_.reset(new TestDriver{propagation_mode, propagation_options, stats_});
  }

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

  Tracing::SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                   start_time_, {Tracing::Reason::Sampling, true});
  first_span->setTag("abc", "123");
  first_span->finishSpan();

  const std::unordered_map<std::string, opentracing::Value> expected_tags = {
      {"abc", std::string{"123"}}};

  EXPECT_EQ(1, driver_->recorder().spans().size());
  EXPECT_EQ(expected_tags, driver_->recorder().top().tags);
}

TEST_F(OpenTracingDriverTest, TagSamplingFalse) {
  setupValidDriver(OpenTracingDriver::PropagationMode::TracerNative, {});

  Tracing::SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                   start_time_, {Tracing::Reason::Sampling, false});
  first_span->finishSpan();

  const std::unordered_map<std::string, opentracing::Value> expected_tags = {
      {opentracing::ext::sampling_priority, 0}};

  EXPECT_EQ(1, driver_->recorder().spans().size());
  EXPECT_EQ(expected_tags, driver_->recorder().top().tags);
}

TEST_F(OpenTracingDriverTest, InjectFailure) {
  for (OpenTracingDriver::PropagationMode propagation_mode :
       {OpenTracingDriver::PropagationMode::SingleHeader,
        OpenTracingDriver::PropagationMode::TracerNative}) {
    opentracing::mocktracer::PropagationOptions propagation_options;
    propagation_options.inject_error_code = std::make_error_code(std::errc::bad_message);
    setupValidDriver(propagation_mode, propagation_options);

    Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, operation_name_,
                                               start_time_, {Tracing::Reason::Sampling, true});

    const auto span_context_injection_error_count =
        stats_.counter("tracing.opentracing.span_context_injection_error").value();
    EXPECT_EQ(nullptr, request_headers_.OtSpanContext());
    span->injectContext(request_headers_);

    EXPECT_EQ(span_context_injection_error_count + 1,
              stats_.counter("tracing.opentracing.span_context_injection_error").value());
  }
}

TEST_F(OpenTracingDriverTest, ExtractWithUnindexedHeader) {
  opentracing::mocktracer::PropagationOptions propagation_options;
  propagation_options.propagation_key = "unindexed-header";
  setupValidDriver(OpenTracingDriver::PropagationMode::TracerNative, propagation_options);

  Tracing::SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                   start_time_, {Tracing::Reason::Sampling, true});
  first_span->injectContext(request_headers_);

  Tracing::SpanPtr second_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                    start_time_, {Tracing::Reason::Sampling, true});
  second_span->finishSpan();
  first_span->finishSpan();

  auto spans = driver_->recorder().spans();
  EXPECT_EQ(spans.at(1).span_context.span_id, spans.at(0).references.at(0).span_id);
}

} // namespace Ot
} // namespace Common
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
