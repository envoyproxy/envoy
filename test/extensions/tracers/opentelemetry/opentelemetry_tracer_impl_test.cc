#include "source/extensions/tracers/opentelemetry/opentelemetry_tracer_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/server/tracer_factory_context.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/utility.h"
#include "test/mocks/stats/mocks.h"
#include "envoy/common/exception.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <sys/types.h>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;
using testing::WithArg;

class OpenTelemetryDriverTest : public testing::Test {
public:
  OpenTelemetryDriverTest() {}

  void setup(envoy::config::trace::v3::OpenTelemetryConfig& opentelemetry_config) {
    auto mock_client_factory = std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
    auto mock_client = std::make_unique<NiceMock<Grpc::MockAsyncClient>>();
    mock_stream_ptr_ = std::make_unique<NiceMock<Grpc::MockAsyncStream>>();
    ON_CALL(*mock_client, startRaw(_, _, _, _)).WillByDefault(Return(mock_stream_ptr_.get()));
    ON_CALL(*mock_client_factory, createUncachedRawAsyncClient())
        .WillByDefault(Return(ByMove(std::move(mock_client))));
    auto& factory_context = context_.server_factory_context_;
    ON_CALL(factory_context, runtime()).WillByDefault(ReturnRef(runtime_));
    ON_CALL(factory_context.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _, _))
        .WillByDefault(Return(ByMove(std::move(mock_client_factory))));
    ON_CALL(factory_context, scope()).WillByDefault(ReturnRef(stats_));

    driver_ = std::make_unique<Driver>(opentelemetry_config, context_);
  }

  void setupValidDriver() {
    const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    )EOF";
    envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
    TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

    setup(opentelemetry_config);
  }

protected:
  const std::string operation_name_{"test"};
  NiceMock<Envoy::Server::Configuration::MockTracerFactoryContext> context_;
  NiceMock<Envoy::Tracing::MockConfig> mock_tracing_config_;
  Event::SimulatedTimeSystem time_system_;
  std::unique_ptr<NiceMock<Grpc::MockAsyncStream>> mock_stream_ptr_{nullptr};
  envoy::config::trace::v3::OpenTelemetryConfig config_;
  Tracing::DriverPtr driver_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockTimer>* timer_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_;
};

TEST_F(OpenTelemetryDriverTest, InitializeDriverValidConfig) {
  setupValidDriver();
  EXPECT_NE(driver_, nullptr);
}

TEST_F(OpenTelemetryDriverTest, ParseSpanContextFromHeadersTest) {
  // Set up driver
  setupValidDriver();

  // Add the OTLP headers to the request headers
  //
  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  // traceparent header is "version-trace_id-parent_id-trace_flags"
  // See https://w3c.github.io/trace-context/#traceparent-header
  const std::string version = "00";
  const uint64_t trace_id_high = 0;
  const uint64_t trace_id_low = 1;
  const std::string trace_id_hex =
      absl::StrCat(Hex::uint64ToHex(trace_id_high), Hex::uint64ToHex(trace_id_low));
  const uint64_t parent_span_id = 2;
  const std::string trace_flags = "01";
  const std::vector<std::string> v = {version, trace_id_hex, Hex::uint64ToHex(parent_span_id),
                                      trace_flags};
  const std::string parent_trace_header = absl::StrJoin(v, "-");

  request_headers.addReferenceKey(OpenTelemetryConstants::get().TRACE_PARENT, parent_trace_header);

  // Mock the random call for generating span ID so we can check it later.
  const u_int64_t new_span_id = 3;
  NiceMock<Random::MockRandomGenerator>& mock_random_generator_ =
      context_.server_factory_context_.api_.random_;
  ON_CALL(mock_random_generator_, random()).WillByDefault(Return(new_span_id));

  Tracing::SpanPtr span =
      driver_->startSpan(mock_tracing_config_, request_headers, operation_name_,
                         time_system_.systemTime(), {Tracing::Reason::Sampling, true});

  // Remove headers, then inject context into header from the span.
  request_headers.remove(OpenTelemetryConstants::get().TRACE_PARENT);
  span->injectContext(request_headers);

  auto sampled_entry = request_headers.get(OpenTelemetryConstants::get().TRACE_PARENT);

  EXPECT_EQ(sampled_entry.size(), 1);
  EXPECT_EQ(
      sampled_entry[0]->value().getStringView(),
      absl::StrJoin({version, trace_id_hex, Hex::uint64ToHex(new_span_id), trace_flags}, "-"));
}

TEST_F(OpenTelemetryDriverTest, GenerateSpanContextWithoutHeadersTest) {
  // Set up driver
  setupValidDriver();

  // Add the OTLP headers to the request headers
  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  // Mock the random call for generating trace and span IDs so we can check it later.
  const uint64_t trace_id_high = 1;
  const uint64_t trace_id_low = 2;
  const uint64_t new_span_id = 3;
  NiceMock<Random::MockRandomGenerator>& mock_random_generator_ =
      context_.server_factory_context_.api_.random_;
  // The tracer should generate three random numbers for the trace high, trace low, and span id.
  {
    InSequence s;

    EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(trace_id_high));
    EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(trace_id_low));
    EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(new_span_id));
  }

  Tracing::SpanPtr span =
      driver_->startSpan(mock_tracing_config_, request_headers, operation_name_,
                         time_system_.systemTime(), {Tracing::Reason::Sampling, true});

  // Remove headers, then inject context into header from the span.
  request_headers.remove(OpenTelemetryConstants::get().TRACE_PARENT);
  span->injectContext(request_headers);

  auto sampled_entry = request_headers.get(OpenTelemetryConstants::get().TRACE_PARENT);

  // Ends in 01 because span should be sampled. See
  // https://w3c.github.io/trace-context/#trace-flags.
  EXPECT_EQ(sampled_entry.size(), 1);
  EXPECT_EQ(sampled_entry[0]->value().getStringView(),
            "00-00000000000000010000000000000002-0000000000000003-01");
}

TEST_F(OpenTelemetryDriverTest, ExportOTLPSpan) {
  // Set up driver
  setupValidDriver();
  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  Tracing::SpanPtr span =
      driver_->startSpan(mock_tracing_config_, request_headers, operation_name_,
                         time_system_.systemTime(), {Tracing::Reason::Sampling, true});
  EXPECT_NE(span.get(), nullptr);

  // Flush after a single span.
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.opentelemetry.min_flush_spans", 5U))
      .Times(1)
      .WillRepeatedly(Return(1));
  // We should see a call to sendMessage to export that single span.
  EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _));
  span->finishSpan();
  EXPECT_EQ(1U, stats_.counter("tracing.opentelemetry.spans_sent").value());
}

TEST_F(OpenTelemetryDriverTest, ExportOTLPSpanWithBuffer) {
  // Set up driver
  setupValidDriver();
  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  Tracing::SpanPtr span =
      driver_->startSpan(mock_tracing_config_, request_headers, operation_name_,
                         time_system_.systemTime(), {Tracing::Reason::Sampling, true});
  EXPECT_NE(span.get(), nullptr);

  // Flush after two spans.
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.opentelemetry.min_flush_spans", 5U))
      .Times(2)
      .WillRepeatedly(Return(2));
  // We should not yet see a call to sendMessage to export that single span.
  span->finishSpan();
  // Once we create a
  Tracing::SpanPtr second_span =
      driver_->startSpan(mock_tracing_config_, request_headers, operation_name_,
                         time_system_.systemTime(), {Tracing::Reason::Sampling, true});
  EXPECT_NE(second_span.get(), nullptr);
  // Only now should we see the span exported.
  EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _));
  second_span->finishSpan();
  EXPECT_EQ(2U, stats_.counter("tracing.opentelemetry.spans_sent").value());
}

TEST_F(OpenTelemetryDriverTest, ExportOTLPSpanWithFlushTimeout) {
  timer_ =
      new NiceMock<Event::MockTimer>(&context_.server_factory_context_.thread_local_.dispatcher_);
  ON_CALL(context_.server_factory_context_.thread_local_.dispatcher_, createTimer_(_))
      .WillByDefault(Invoke([this](Event::TimerCb) { return timer_; }));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5000), _));
  // Set up driver
  setupValidDriver();
  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  Tracing::SpanPtr span =
      driver_->startSpan(mock_tracing_config_, request_headers, operation_name_,
                         time_system_.systemTime(), {Tracing::Reason::Sampling, true});
  EXPECT_NE(span.get(), nullptr);

  // Set it to flush after 2 spans so that the span will only be flushed by timeout.
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.opentelemetry.min_flush_spans", 5U))
      .Times(1)
      .WillRepeatedly(Return(2));
  // We should not yet see a call to sendMessage to export that single span.
  span->finishSpan();
  // Only now should we see the span exported.
  EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _));
  // Timer should be reenabled.
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5000), _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.opentelemetry.flush_interval_ms", 5000U))
      .WillOnce(Return(5000U));
  timer_->invokeCallback();
  EXPECT_EQ(1U, stats_.counter("tracing.opentelemetry.spans_sent").value());
  EXPECT_EQ(1U, stats_.counter("tracing.opentelemetry.timer_flushed").value());
}

TEST_F(OpenTelemetryDriverTest, SpawnChildSpan) {
  // Set up driver
  setupValidDriver();
  Http::TestRequestHeaderMapImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  // Mock the random call for generating the parent span's IDs so we can check it later.
  const uint64_t parent_trace_id_high = 0;
  const uint64_t parent_trace_id_low = 2;
  const uint64_t parent_span_id = 3;
  NiceMock<Random::MockRandomGenerator>& mock_random_generator_ =
      context_.server_factory_context_.api_.random_;
  {
    InSequence s;

    EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(parent_trace_id_high));
    EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(parent_trace_id_low));
    EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(parent_span_id));
  }

  Tracing::SpanPtr span =
      driver_->startSpan(mock_tracing_config_, request_headers, operation_name_,
                         time_system_.systemTime(), {Tracing::Reason::Sampling, true});
  EXPECT_NE(span.get(), nullptr);

  // The child should only generate a span ID for itself; the trace id should come from the parent..
  const uint64_t child_span_id = 3;
  EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(child_span_id));
  Tracing::SpanPtr child_span =
      span->spawnChild(mock_tracing_config_, operation_name_, time_system_.systemTime());

  // Flush after a single span.
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.opentelemetry.min_flush_spans", 5U))
      .Times(1)
      .WillRepeatedly(Return(1));
  // We should see a call to sendMessage to export that single span.
  EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _));
  child_span->finishSpan();
  EXPECT_EQ(1U, stats_.counter("tracing.opentelemetry.spans_sent").value());
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
