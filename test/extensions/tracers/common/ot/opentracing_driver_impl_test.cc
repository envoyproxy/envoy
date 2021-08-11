#include <memory>

#include "source/extensions/tracers/common/ot/opentracing_driver_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/tracing/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentracing/mocktracer/in_memory_recorder.h"
#include "opentracing/mocktracer/tracer.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Common {
namespace Ot {
namespace {

class TestDriver : public OpenTracingDriver {
public:
  TestDriver(OpenTracingDriver::PropagationMode propagation_mode,
             const opentracing::mocktracer::PropagationOptions& propagation_options,
             Stats::Scope& scope)
      : OpenTracingDriver{scope}, propagation_mode_{propagation_mode} {
    opentracing::mocktracer::MockTracerOptions options;
    auto recorder = new opentracing::mocktracer::InMemoryRecorder{};
    recorder_ = recorder;
    options.recorder.reset(recorder);
    options.propagation_options = propagation_options;
    tracer_ = std::make_shared<opentracing::mocktracer::MockTracer>(std::move(options));
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

class OpenTracingDriverTest : public testing::Test {
public:
  void
  setupValidDriver(OpenTracingDriver::PropagationMode propagation_mode =
                       OpenTracingDriver::PropagationMode::SingleHeader,
                   const opentracing::mocktracer::PropagationOptions& propagation_options = {}) {
    driver_ = std::make_unique<TestDriver>(propagation_mode, propagation_options, stats_);
  }

  const std::string operation_name_{"test"};
  Http::TestRequestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  const Http::TestResponseHeaderMapImpl response_headers_{{":status", "500"}};
  SystemTime start_time_;

  std::unique_ptr<TestDriver> driver_;
  Stats::TestUtil::TestStore stats_;

  NiceMock<Tracing::MockConfig> config_;
};

TEST_F(OpenTracingDriverTest, FlushSpanWithTag) {
  setupValidDriver();

  Tracing::SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                   start_time_, {Tracing::Reason::Sampling, true});
  first_span->setTag("abc", "123");
  first_span->finishSpan();

  const std::map<std::string, opentracing::Value> expected_tags = {
      {"abc", std::string{"123"}},
      {opentracing::ext::span_kind, std::string{opentracing::ext::span_kind_rpc_server}}};

  EXPECT_EQ(1, driver_->recorder().spans().size());
  EXPECT_EQ(expected_tags, driver_->recorder().top().tags);
}

TEST_F(OpenTracingDriverTest, FlushSpanWithLog) {
  setupValidDriver();

  Tracing::SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                   start_time_, {Tracing::Reason::Sampling, true});
  const auto timestamp =
      SystemTime{std::chrono::duration_cast<SystemTime::duration>(std::chrono::hours{123})};
  first_span->log(timestamp, "abc");
  first_span->finishSpan();

  const std::vector<opentracing::LogRecord> expected_logs = {
      {timestamp, {{"event", std::string{"abc"}}}}};

  EXPECT_EQ(1, driver_->recorder().spans().size());
  EXPECT_EQ(expected_logs, driver_->recorder().top().logs);
}

TEST_F(OpenTracingDriverTest, FlushSpanWithBaggage) {
  setupValidDriver();

  Tracing::SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                   start_time_, {Tracing::Reason::Sampling, true});
  first_span->setBaggage("abc", "123");
  first_span->finishSpan();

  const std::map<std::string, std::string> expected_baggage = {{"abc", "123"}};

  EXPECT_EQ(1, driver_->recorder().spans().size());
  EXPECT_EQ(expected_baggage, driver_->recorder().top().span_context.baggage);
}

TEST_F(OpenTracingDriverTest, TagSamplingFalseByDecision) {
  setupValidDriver(OpenTracingDriver::PropagationMode::TracerNative, {});

  Tracing::SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                   start_time_, {Tracing::Reason::Sampling, false});
  first_span->finishSpan();

  const std::map<std::string, opentracing::Value> expected_tags = {
      {opentracing::ext::sampling_priority, 0},
      {opentracing::ext::span_kind, std::string{opentracing::ext::span_kind_rpc_server}}};

  EXPECT_EQ(1, driver_->recorder().spans().size());
  EXPECT_EQ(expected_tags, driver_->recorder().top().tags);
}

TEST_F(OpenTracingDriverTest, TagSamplingFalseByFlag) {
  setupValidDriver(OpenTracingDriver::PropagationMode::TracerNative, {});

  Tracing::SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                   start_time_, {Tracing::Reason::Sampling, true});
  first_span->setSampled(false);
  first_span->finishSpan();

  const std::map<std::string, opentracing::Value> expected_tags = {
      {opentracing::ext::sampling_priority, 0},
      {opentracing::ext::span_kind, std::string{opentracing::ext::span_kind_rpc_server}}};

  EXPECT_EQ(1, driver_->recorder().spans().size());
  EXPECT_EQ(expected_tags, driver_->recorder().top().tags);
}

TEST_F(OpenTracingDriverTest, TagSpanKindClient) {
  setupValidDriver(OpenTracingDriver::PropagationMode::TracerNative, {});

  ON_CALL(config_, operationName()).WillByDefault(testing::Return(Tracing::OperationName::Egress));

  Tracing::SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                   start_time_, {Tracing::Reason::Sampling, true});
  first_span->finishSpan();

  const std::map<std::string, opentracing::Value> expected_tags = {
      {opentracing::ext::span_kind, std::string{opentracing::ext::span_kind_rpc_client}}};

  EXPECT_EQ(1, driver_->recorder().spans().size());
  EXPECT_EQ(expected_tags, driver_->recorder().top().tags);
}

TEST_F(OpenTracingDriverTest, TagSpanKindServer) {
  setupValidDriver(OpenTracingDriver::PropagationMode::TracerNative, {});

  ON_CALL(config_, operationName()).WillByDefault(testing::Return(Tracing::OperationName::Ingress));

  Tracing::SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                   start_time_, {Tracing::Reason::Sampling, true});
  first_span->finishSpan();

  const std::map<std::string, opentracing::Value> expected_tags = {
      {opentracing::ext::span_kind, std::string{opentracing::ext::span_kind_rpc_server}}};

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
    EXPECT_FALSE(request_headers_.has(Http::CustomHeaders::get().OtSpanContext));
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

TEST_F(OpenTracingDriverTest, GetTraceId) {
  setupValidDriver();

  Tracing::SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_,
                                                   start_time_, {Tracing::Reason::Sampling, true});
  first_span->setTag("abc", "123");
  first_span->finishSpan();

  // This method is unimplemented and a noop.
  ASSERT_EQ(first_span->getTraceIdAsHex(), "");
}

} // namespace
} // namespace Ot
} // namespace Common
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
