#include <memory>
#include <utility>
#include <vector>

#include "source/extensions/tracers/common/ot/opentracing_driver_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
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

class NullSpanContext : public opentracing::SpanContext {
public:
  void ForeachBaggageItem(
      std::function<bool(const std::string& key, const std::string& value)>) const override {
    // not implemented
  }

  virtual std::unique_ptr<opentracing::SpanContext> clone() const noexcept {
    return std::make_unique<NullSpanContext>(*this);
  }
};

class NullSpan : public opentracing::Span {
public:
  explicit NullSpan(const opentracing::Tracer& tracer) : tracer_(tracer) {}

  void FinishWithOptions(const opentracing::FinishSpanOptions&) noexcept override {
    // not implemented
  }

  void SetOperationName(opentracing::string_view /*name*/) noexcept override {
    // not implemented
  }

  void SetTag(opentracing::string_view /*key*/,
              const opentracing::Value& /*value*/) noexcept override {
    // not implemented
  }

  void SetBaggageItem(opentracing::string_view /*key*/,
                      opentracing::string_view /*value*/) noexcept override {
    // not implemented
  }

  std::string BaggageItem(opentracing::string_view /*key*/) const noexcept override {
    return ""; // not implemented
  }

  void Log(std::initializer_list<
           std::pair<opentracing::string_view, opentracing::Value>> /*fields*/) noexcept override {
    // not implemented
  }

  const opentracing::SpanContext& context() const noexcept override {
    static NullSpanContext context;
    return context;
  }

  const opentracing::Tracer& tracer() const noexcept override { return tracer_; }

  const opentracing::Tracer& tracer_;
};

class ContextIteratingTracer : public opentracing::Tracer {
public:
  explicit ContextIteratingTracer(
      std::vector<std::pair<std::string, std::string>>& extracted_headers_destination)
      : extracted_headers_(&extracted_headers_destination) {}

  std::unique_ptr<opentracing::Span>
  StartSpanWithOptions(opentracing::string_view /*operation_name*/,
                       const opentracing::StartSpanOptions&) const noexcept override {
    return std::make_unique<NullSpan>(*this);
  }

  opentracing::expected<void> Inject(const opentracing::SpanContext&,
                                     std::ostream& /*writer*/) const override {
    return {}; // not implemented
  }

  opentracing::expected<void> Inject(const opentracing::SpanContext&,
                                     const opentracing::TextMapWriter&) const override {
    return {}; // not implemented
  }

  opentracing::expected<void> Inject(const opentracing::SpanContext&,
                                     const opentracing::HTTPHeadersWriter&) const override {
    return {}; // not implemented
  }

  opentracing::expected<void>
  Inject(const opentracing::SpanContext& sc,
         const opentracing::CustomCarrierWriter& writer) const override {
    return opentracing::Tracer::Inject(sc, writer);
  }

  opentracing::expected<std::unique_ptr<opentracing::SpanContext>>
  Extract(std::istream& /*reader*/) const override {
    return std::unique_ptr<opentracing::SpanContext>(); // not implemented
  }

  opentracing::expected<std::unique_ptr<opentracing::SpanContext>>
  Extract(const opentracing::TextMapReader&) const override {
    return std::unique_ptr<opentracing::SpanContext>(); // not implemented
  }

  opentracing::expected<std::unique_ptr<opentracing::SpanContext>>
  Extract(const opentracing::HTTPHeadersReader& reader) const override {
    reader.ForeachKey([this](opentracing::string_view key,
                             opentracing::string_view value) -> opentracing::expected<void> {
      extracted_headers_->emplace_back(key, value);
      return {};
    });

    return std::unique_ptr<opentracing::SpanContext>();
  }

  opentracing::expected<std::unique_ptr<opentracing::SpanContext>>
  Extract(const opentracing::CustomCarrierReader& reader) const override {
    return opentracing::Tracer::Extract(reader);
  }

  std::vector<std::pair<std::string, std::string>>* extracted_headers_;
};

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

  TestDriver(const std::shared_ptr<ContextIteratingTracer>& tracer, Stats::Scope& scope)
      : OpenTracingDriver{scope},
        propagation_mode_{PropagationMode::TracerNative}, recorder_{nullptr}, tracer_{tracer} {}

  const opentracing::mocktracer::InMemoryRecorder& recorder() const { return *recorder_; }

  // OpenTracingDriver
  opentracing::Tracer& tracer() override { return *tracer_; }

  PropagationMode propagationMode() const override { return propagation_mode_; }

private:
  const OpenTracingDriver::PropagationMode propagation_mode_;
  const opentracing::mocktracer::InMemoryRecorder* recorder_;
  std::shared_ptr<opentracing::Tracer> tracer_;
};

class OpenTracingDriverTest : public testing::Test {
public:
  void
  setupValidDriver(OpenTracingDriver::PropagationMode propagation_mode =
                       OpenTracingDriver::PropagationMode::SingleHeader,
                   const opentracing::mocktracer::PropagationOptions& propagation_options = {}) {
    driver_ =
        std::make_unique<TestDriver>(propagation_mode, propagation_options, *stats_.rootScope());
  }

  void setupValidDriver(std::vector<std::pair<std::string, std::string>>& headers_destination) {
    auto tracer = std::make_shared<ContextIteratingTracer>(headers_destination);
    driver_ = std::make_unique<TestDriver>(tracer, *stats_.rootScope());
  }

  const std::string operation_name_{"test"};
  Tracing::TestTraceContextImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;

  std::unique_ptr<TestDriver> driver_;
  Stats::TestUtil::TestStore stats_;

  NiceMock<Tracing::MockConfig> config_;
};

TEST_F(OpenTracingDriverTest, FlushSpanWithTag) {
  setupValidDriver();

  Tracing::SpanPtr first_span = driver_->startSpan(
      config_, request_headers_, stream_info_, operation_name_, {Tracing::Reason::Sampling, true});
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

  Tracing::SpanPtr first_span = driver_->startSpan(
      config_, request_headers_, stream_info_, operation_name_, {Tracing::Reason::Sampling, true});
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

  Tracing::SpanPtr first_span = driver_->startSpan(
      config_, request_headers_, stream_info_, operation_name_, {Tracing::Reason::Sampling, true});
  first_span->setBaggage("abc", "123");
  first_span->finishSpan();

  const std::map<std::string, std::string> expected_baggage = {{"abc", "123"}};

  EXPECT_EQ(1, driver_->recorder().spans().size());
  EXPECT_EQ(expected_baggage, driver_->recorder().top().span_context.baggage);
}

TEST_F(OpenTracingDriverTest, TagSamplingFalseByDecision) {
  setupValidDriver(OpenTracingDriver::PropagationMode::TracerNative, {});

  Tracing::SpanPtr first_span = driver_->startSpan(
      config_, request_headers_, stream_info_, operation_name_, {Tracing::Reason::Sampling, false});
  first_span->finishSpan();

  const std::map<std::string, opentracing::Value> expected_tags = {
      {opentracing::ext::sampling_priority, 0},
      {opentracing::ext::span_kind, std::string{opentracing::ext::span_kind_rpc_server}}};

  EXPECT_EQ(1, driver_->recorder().spans().size());
  EXPECT_EQ(expected_tags, driver_->recorder().top().tags);
}

TEST_F(OpenTracingDriverTest, TagSamplingFalseByFlag) {
  setupValidDriver(OpenTracingDriver::PropagationMode::TracerNative, {});

  Tracing::SpanPtr first_span = driver_->startSpan(
      config_, request_headers_, stream_info_, operation_name_, {Tracing::Reason::Sampling, true});
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

  Tracing::SpanPtr first_span = driver_->startSpan(
      config_, request_headers_, stream_info_, operation_name_, {Tracing::Reason::Sampling, true});
  first_span->finishSpan();

  const std::map<std::string, opentracing::Value> expected_tags = {
      {opentracing::ext::span_kind, std::string{opentracing::ext::span_kind_rpc_client}}};

  EXPECT_EQ(1, driver_->recorder().spans().size());
  EXPECT_EQ(expected_tags, driver_->recorder().top().tags);
}

TEST_F(OpenTracingDriverTest, TagSpanKindServer) {
  setupValidDriver(OpenTracingDriver::PropagationMode::TracerNative, {});

  ON_CALL(config_, operationName()).WillByDefault(testing::Return(Tracing::OperationName::Ingress));

  Tracing::SpanPtr first_span = driver_->startSpan(
      config_, request_headers_, stream_info_, operation_name_, {Tracing::Reason::Sampling, true});
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

    Tracing::SpanPtr span = driver_->startSpan(config_, request_headers_, stream_info_,
                                               operation_name_, {Tracing::Reason::Sampling, true});

    const auto span_context_injection_error_count =
        stats_.counter("tracing.opentracing.span_context_injection_error").value();
    EXPECT_FALSE(
        request_headers_.context_map_.contains(Http::CustomHeaders::get().OtSpanContext.get()));
    span->injectContext(request_headers_, Tracing::UpstreamContext());

    EXPECT_EQ(span_context_injection_error_count + 1,
              stats_.counter("tracing.opentracing.span_context_injection_error").value());
  }
}

TEST_F(OpenTracingDriverTest, ExtractWithUnindexedHeader) {
  opentracing::mocktracer::PropagationOptions propagation_options;
  propagation_options.propagation_key = "unindexed-header";
  setupValidDriver(OpenTracingDriver::PropagationMode::TracerNative, propagation_options);

  Tracing::SpanPtr first_span = driver_->startSpan(
      config_, request_headers_, stream_info_, operation_name_, {Tracing::Reason::Sampling, true});
  first_span->injectContext(request_headers_, Tracing::UpstreamContext());

  Tracing::SpanPtr second_span = driver_->startSpan(
      config_, request_headers_, stream_info_, operation_name_, {Tracing::Reason::Sampling, true});
  second_span->finishSpan();
  first_span->finishSpan();

  auto spans = driver_->recorder().spans();
  EXPECT_EQ(spans.at(1).span_context.span_id, spans.at(0).references.at(0).span_id);
}

TEST_F(OpenTracingDriverTest, GetTraceId) {
  setupValidDriver();

  Tracing::SpanPtr first_span = driver_->startSpan(
      config_, request_headers_, stream_info_, operation_name_, {Tracing::Reason::Sampling, true});
  first_span->setTag("abc", "123");
  first_span->finishSpan();

  // This method is unimplemented and a noop.
  ASSERT_EQ(first_span->getTraceId(), "");
  // This method is unimplemented and a noop.
  ASSERT_EQ(first_span->getSpanId(), "");
}

TEST_F(OpenTracingDriverTest, ExtractUsingForeach) {
  std::vector<std::pair<std::string, std::string>> extracted_headers;
  setupValidDriver(extracted_headers);

  // Starting a new span, given the `request_headers_`, will visit the headers
  // using "for each." We can immediately discard the span.
  driver_->startSpan(config_, request_headers_, stream_info_, operation_name_,
                     {Tracing::Reason::Sampling, true});

  for (const auto& [key, value] : extracted_headers) {
    EXPECT_EQ(value, request_headers_.get(key));
  }
}

} // namespace
} // namespace Ot
} // namespace Common
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
