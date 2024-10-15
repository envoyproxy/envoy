#include <sys/types.h>

#include "envoy/common/exception.h"

#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/tracers/opentelemetry/opentelemetry_tracer_impl.h"
#include "source/extensions/tracers/opentelemetry/span_context_extractor.h"

#include "test/mocks/common.h"
#include "test/mocks/server/tracer_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

class MockResourceProvider : public ResourceProvider {
public:
  MOCK_METHOD(Resource, getResource,
              (const envoy::config::trace::v3::OpenTelemetryConfig& opentelemetry_config,
               Server::Configuration::TracerFactoryContext& context),
              (const));
};

class OpenTelemetryDriverTest : public testing::Test {
public:
  OpenTelemetryDriverTest() = default;

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
    ON_CALL(factory_context, scope()).WillByDefault(ReturnRef(scope_));

    Resource resource;
    resource.attributes_.insert(std::pair<std::string, std::string>("key1", "val1"));

    auto mock_resource_provider = NiceMock<MockResourceProvider>();
    EXPECT_CALL(mock_resource_provider, getResource(_, _)).WillRepeatedly(Return(resource));

    driver_ = std::make_unique<Driver>(opentelemetry_config, context_, mock_resource_provider);
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

  void setupValidDriverWithHttpExporter() {
    const std::string yaml_string = R"EOF(
    http_service:
      http_uri:
        cluster: "my_o11y_backend"
        uri: "https://some-o11y.com/otlp/v1/traces"
        timeout: 0.250s
      request_headers_to_add:
      - header:
          key: "Authorization"
          value: "auth-token"
    )EOF";
    envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
    TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

    setup(opentelemetry_config);
  }

protected:
  const std::string operation_name_{"test"};
  NiceMock<Envoy::Server::Configuration::MockTracerFactoryContext> context_;
  NiceMock<Envoy::Tracing::MockConfig> mock_tracing_config_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  Event::SimulatedTimeSystem time_system_;
  std::unique_ptr<NiceMock<Grpc::MockAsyncStream>> mock_stream_ptr_{nullptr};
  envoy::config::trace::v3::OpenTelemetryConfig config_;
  Tracing::DriverPtr driver_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockTimer>* timer_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_;
  Stats::Scope& scope_{*stats_.rootScope()};
};

// Tests the tracer initialization with the gRPC exporter
TEST_F(OpenTelemetryDriverTest, InitializeDriverValidConfig) {
  setupValidDriver();
  EXPECT_NE(driver_, nullptr);
}

// Tests the tracer initialization with the HTTP exporter
TEST_F(OpenTelemetryDriverTest, InitializeDriverValidConfigHttpExporter) {
  setupValidDriverWithHttpExporter();
  EXPECT_NE(driver_, nullptr);
}

// Verifies that the tracer cannot be configured with two exporters at the same time
TEST_F(OpenTelemetryDriverTest, BothGrpcAndHttpExportersConfigured) {
  const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    http_service:
      http_uri:
        cluster: "my_o11y_backend"
        uri: "https://some-o11y.com/otlp/v1/traces"
        timeout: 0.250s
    )EOF";
  envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
  TestUtility::loadFromYaml(yaml_string, opentelemetry_config);

  EXPECT_THROW_WITH_MESSAGE(setup(opentelemetry_config), EnvoyException,
                            "OpenTelemetry Tracer cannot have both gRPC and HTTP exporters "
                            "configured. OpenTelemetry tracer will be disabled.");
  EXPECT_EQ(driver_, nullptr);
}

// Verifies traceparent/tracestate headers are properly parsed and propagated
TEST_F(OpenTelemetryDriverTest, ParseSpanContextFromHeadersTest) {
  // Set up driver
  setupValidDriver();

  // Add the OTLP headers to the request headers
  Tracing::TestTraceContextImpl request_headers{
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
  request_headers.set(OpenTelemetryConstants::get().TRACE_PARENT.key(), parent_trace_header);
  // Also add tracestate.
  request_headers.set(OpenTelemetryConstants::get().TRACE_STATE.key(), "test=foo");

  // Mock the random call for generating span ID so we can check it later.
  const uint64_t new_span_id = 3;
  NiceMock<Random::MockRandomGenerator>& mock_random_generator_ =
      context_.server_factory_context_.api_.random_;
  ON_CALL(mock_random_generator_, random()).WillByDefault(Return(new_span_id));

  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, request_headers, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});

  EXPECT_EQ(span->getTraceId(), trace_id_hex);

  // Remove headers, then inject context into header from the span.
  request_headers.remove(OpenTelemetryConstants::get().TRACE_PARENT.key());
  request_headers.remove(OpenTelemetryConstants::get().TRACE_STATE.key());
  span->injectContext(request_headers, Tracing::UpstreamContext());

  auto sampled_entry = request_headers.get(OpenTelemetryConstants::get().TRACE_PARENT.key());
  EXPECT_EQ(sampled_entry.has_value(), true);
  EXPECT_EQ(
      sampled_entry.value(),
      absl::StrJoin({version, trace_id_hex, Hex::uint64ToHex(new_span_id), trace_flags}, "-"));

  auto sampled_tracestate_entry =
      request_headers.get(OpenTelemetryConstants::get().TRACE_STATE.key());
  EXPECT_EQ(sampled_tracestate_entry.has_value(), true);
  EXPECT_EQ(sampled_tracestate_entry.value(), "test=foo");
  constexpr absl::string_view request_yaml = R"(
resource_spans:
  resource:
    attributes:
      key: "service.name"
      value:
        string_value: "unknown_service:envoy"
      key: "key1"
      value:
        string_value: "val1"
  scope_spans:
    spans:
      trace_id: "AAA"
      span_id: "AAA"
      name: "test"
      kind: SPAN_KIND_SERVER
      start_time_unix_nano: {}
      end_time_unix_nano: {}
      trace_state: "test=foo"
  )";
  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest request_proto;
  SystemTime timestamp = time_system_.systemTime();
  ON_CALL(stream_info_, startTime()).WillByDefault(Return(timestamp));

  int64_t timestamp_ns = std::chrono::nanoseconds(timestamp.time_since_epoch()).count();
  TestUtility::loadFromYaml(fmt::format(request_yaml, timestamp_ns, timestamp_ns), request_proto);
  auto* expected_span =
      request_proto.mutable_resource_spans(0)->mutable_scope_spans(0)->mutable_spans(0);
  expected_span->set_trace_id(absl::HexStringToBytes(trace_id_hex));
  expected_span->set_span_id(absl::HexStringToBytes(absl::StrCat(Hex::uint64ToHex(new_span_id))));
  expected_span->set_parent_span_id(
      absl::HexStringToBytes(absl::StrCat(Hex::uint64ToHex(parent_span_id))));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.opentelemetry.min_flush_spans", 5U))
      .Times(1)
      .WillRepeatedly(Return(1));
  EXPECT_CALL(*mock_stream_ptr_,
              sendMessageRaw_(Grpc::ProtoBufferEqIgnoreRepeatedFieldOrdering(request_proto), _));
  span->finishSpan();
  EXPECT_EQ(1U, stats_.counter("tracing.opentelemetry.spans_sent").value());
}

// Verifies span is properly created when the incoming request has no traceparent/tracestate headers
TEST_F(OpenTelemetryDriverTest, GenerateSpanContextWithoutHeadersTest) {
  // Set up driver
  setupValidDriver();

  // Add the OTLP headers to the request headers
  Tracing::TestTraceContextImpl request_headers{
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

  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, request_headers, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});

  // Remove headers, then inject context into header from the span.
  request_headers.remove(OpenTelemetryConstants::get().TRACE_PARENT.key());
  span->injectContext(request_headers, Tracing::UpstreamContext());

  auto sampled_entry = request_headers.get(OpenTelemetryConstants::get().TRACE_PARENT.key());

  // Ends in 01 because span should be sampled. See
  // https://w3c.github.io/trace-context/#trace-flags.
  EXPECT_EQ(sampled_entry.has_value(), true);
  EXPECT_EQ(sampled_entry.value(), "00-00000000000000010000000000000002-0000000000000003-01");
}

// Verifies a span it not created when an invalid traceparent header is received
TEST_F(OpenTelemetryDriverTest, NullSpanWithPropagationHeaderError) {
  setupValidDriver();
  // Add an invalid OTLP header to the request headers.
  Tracing::TestTraceContextImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};
  request_headers.set(OpenTelemetryConstants::get().TRACE_PARENT.key(),
                      "invalid00-0000000000000003-01");

  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, request_headers, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});

  auto& null_span = *span;
  EXPECT_EQ(typeid(null_span).name(), typeid(Tracing::NullSpan).name());
}

// Verifies the export happens after one span is created
TEST_F(OpenTelemetryDriverTest, ExportOTLPSpan) {
  // Set up driver
  setupValidDriver();
  Tracing::TestTraceContextImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, request_headers, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  EXPECT_NE(span.get(), nullptr);

  // Test baggage noop and other noop calls.
  span->setBaggage("baggage_key", "baggage_value");
  EXPECT_TRUE(span->getBaggage("baggage_key").empty());
  span->setOperation("operation");
  span->setTag("tag_name", "tag_value");
  span->log(time_system_.systemTime(), "event");

  // Flush after a single span.
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.opentelemetry.min_flush_spans", 5U))
      .Times(1)
      .WillRepeatedly(Return(1));
  // We should see a call to sendMessage to export that single span.
  EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _));
  span->finishSpan();
  EXPECT_EQ(1U, stats_.counter("tracing.opentelemetry.spans_sent").value());
}

// Verifies the export happens only when a second span is created
TEST_F(OpenTelemetryDriverTest, ExportOTLPSpanWithBuffer) {
  // Set up driver
  setupValidDriver();
  Tracing::TestTraceContextImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, request_headers, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  EXPECT_NE(span.get(), nullptr);

  // Flush after two spans.
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.opentelemetry.min_flush_spans", 5U))
      .Times(2)
      .WillRepeatedly(Return(2));
  // We should not yet see a call to sendMessage to export that single span.
  span->finishSpan();
  // Once we create a
  Tracing::SpanPtr second_span =
      driver_->startSpan(mock_tracing_config_, request_headers, stream_info_, operation_name_,
                         {Tracing::Reason::Sampling, true});
  EXPECT_NE(second_span.get(), nullptr);
  // Only now should we see the span exported.
  EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _));
  second_span->finishSpan();
  EXPECT_EQ(2U, stats_.counter("tracing.opentelemetry.spans_sent").value());
}

// Verifies the export happens after a timeout
TEST_F(OpenTelemetryDriverTest, ExportOTLPSpanWithFlushTimeout) {
  timer_ =
      new NiceMock<Event::MockTimer>(&context_.server_factory_context_.thread_local_.dispatcher_);
  ON_CALL(context_.server_factory_context_.thread_local_.dispatcher_, createTimer_(_))
      .WillByDefault(Invoke([this](Event::TimerCb) { return timer_; }));
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5000), _));
  // Set up driver
  setupValidDriver();
  Tracing::TestTraceContextImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, request_headers, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  EXPECT_NE(span.get(), nullptr);

  // Set it to flush after 2 spans so that the span will only be flushed by timeout.
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.opentelemetry.min_flush_spans", 5U))
      .Times(1)
      .WillRepeatedly(Return(2));
  // We should not yet see a call to sendMessage to export that single span.
  span->finishSpan();
  // Only now should we see the span exported.
  EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _));
  // Timer should be enabled again.
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5000), _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.opentelemetry.flush_interval_ms", 5000U))
      .WillOnce(Return(5000U));
  timer_->invokeCallback();
  EXPECT_EQ(1U, stats_.counter("tracing.opentelemetry.spans_sent").value());
  EXPECT_EQ(1U, stats_.counter("tracing.opentelemetry.timer_flushed").value());
}

// Verifies child span is related to parent span
TEST_F(OpenTelemetryDriverTest, SpawnChildSpan) {
  // Set up driver
  setupValidDriver();
  Tracing::TestTraceContextImpl request_headers{
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

  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, request_headers, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
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

// Verifies the span types
TEST_F(OpenTelemetryDriverTest, SpanType) {
  // Set up driver
  setupValidDriver();
  Tracing::TestTraceContextImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  // Mock the random call for generating the parent span's IDs so we can check it later.
  const uint64_t parent_trace_id_high = 0;
  const uint64_t parent_trace_id_low = 2;
  const uint64_t parent_span_id = 3;

  {
    mock_tracing_config_.spawn_upstream_span_ = false;
    mock_tracing_config_.operation_name_ = Tracing::OperationName::Ingress;

    NiceMock<Random::MockRandomGenerator>& mock_random_generator_ =
        context_.server_factory_context_.api_.random_;
    {
      InSequence s;

      EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(parent_trace_id_high));
      EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(parent_trace_id_low));
      EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(parent_span_id));
    }

    Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, request_headers, stream_info_,
                                               operation_name_, {Tracing::Reason::Sampling, true});
    EXPECT_NE(span.get(), nullptr);

    // The span type should be SERVER because spawn_upstream_span_ is false and operation_name_ is
    // Ingress.
    EXPECT_EQ(dynamic_cast<Span*>(span.get())->spanForTest().kind(),
              ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER);

    // The child should only generate a span ID for itself; the trace id should come from the
    // parent.
    const uint64_t child_span_id = 3;
    EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(child_span_id));
    Tracing::SpanPtr child_span =
        span->spawnChild(mock_tracing_config_, operation_name_, time_system_.systemTime());

    // The child span should also be a CLIENT span.
    EXPECT_EQ(dynamic_cast<Span*>(child_span.get())->spanForTest().kind(),
              ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_CLIENT);
  }

  {
    mock_tracing_config_.spawn_upstream_span_ = false;
    mock_tracing_config_.operation_name_ = Tracing::OperationName::Egress;

    NiceMock<Random::MockRandomGenerator>& mock_random_generator_ =
        context_.server_factory_context_.api_.random_;
    {
      InSequence s;

      EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(parent_trace_id_high));
      EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(parent_trace_id_low));
      EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(parent_span_id));
    }

    Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, request_headers, stream_info_,
                                               operation_name_, {Tracing::Reason::Sampling, true});
    EXPECT_NE(span.get(), nullptr);

    // The span type should be CLIENT because spawn_upstream_span_ is false and operation_name_ is
    // Egress.
    EXPECT_EQ(dynamic_cast<Span*>(span.get())->spanForTest().kind(),
              ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_CLIENT);

    // The child should only generate a span ID for itself; the trace id should come from the
    // parent.
    const uint64_t child_span_id = 3;
    EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(child_span_id));
    Tracing::SpanPtr child_span =
        span->spawnChild(mock_tracing_config_, operation_name_, time_system_.systemTime());

    // The child span should also be a CLIENT span.
    EXPECT_EQ(dynamic_cast<Span*>(child_span.get())->spanForTest().kind(),
              ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_CLIENT);
  }

  {
    mock_tracing_config_.spawn_upstream_span_ = true;
    mock_tracing_config_.operation_name_ = Tracing::OperationName::Egress;

    NiceMock<Random::MockRandomGenerator>& mock_random_generator_ =
        context_.server_factory_context_.api_.random_;
    {
      InSequence s;

      EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(parent_trace_id_high));
      EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(parent_trace_id_low));
      EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(parent_span_id));
    }

    Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, request_headers, stream_info_,
                                               operation_name_, {Tracing::Reason::Sampling, true});
    EXPECT_NE(span.get(), nullptr);

    // The span type should be SERVER because spawn_upstream_span_ is true and this is
    // downstream span.
    EXPECT_EQ(dynamic_cast<Span*>(span.get())->spanForTest().kind(),
              ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_SERVER);

    // The child should only generate a span ID for itself; the trace id should come from the
    // parent.
    const uint64_t child_span_id = 3;
    EXPECT_CALL(mock_random_generator_, random()).WillOnce(Return(child_span_id));
    Tracing::SpanPtr child_span =
        span->spawnChild(mock_tracing_config_, operation_name_, time_system_.systemTime());

    child_span->setTag("http.status_code", "400");

    // The child span should also be a CLIENT span.
    EXPECT_EQ(dynamic_cast<Span*>(child_span.get())->spanForTest().kind(),
              ::opentelemetry::proto::trace::v1::Span::SPAN_KIND_CLIENT);
    // The child span should have an error status.
    EXPECT_EQ(dynamic_cast<Span*>(child_span.get())->spanForTest().status().code(),
              ::opentelemetry::proto::trace::v1::Status::STATUS_CODE_ERROR);
  }
}

// Verifies spans are exported with their attributes
TEST_F(OpenTelemetryDriverTest, ExportOTLPSpanWithAttributes) {
  setupValidDriver();
  Tracing::TestTraceContextImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};
  NiceMock<Random::MockRandomGenerator>& mock_random_generator_ =
      context_.server_factory_context_.api_.random_;
  int64_t generated_int = 1;
  EXPECT_CALL(mock_random_generator_, random()).Times(3).WillRepeatedly(Return(generated_int));
  SystemTime timestamp = time_system_.systemTime();
  ON_CALL(stream_info_, startTime()).WillByDefault(Return(timestamp));

  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, request_headers, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  EXPECT_NE(span.get(), nullptr);

  span->setTag("first_tag_name", "first_tag_value");
  span->setTag("second_tag_name", "second_tag_value");
  // Try an empty tag.
  span->setTag("", "empty_tag_value");
  // Overwrite a tag.
  span->setTag("first_tag_name", "first_tag_new_value");

  // Note the placeholders for the bytes - cleaner to manually set after.
  constexpr absl::string_view request_yaml = R"(
resource_spans:
  resource:
    attributes:
      key: "service.name"
      value:
        string_value: "unknown_service:envoy"
      key: "key1"
      value:
        string_value: "val1"
  scope_spans:
    spans:
      trace_id: "AAA"
      span_id: "AAA"
      name: "test"
      kind: SPAN_KIND_SERVER
      start_time_unix_nano: {}
      end_time_unix_nano: {}
      attributes:
        - key: "first_tag_name"
          value:
            string_value: "first_tag_new_value"
        - key: "second_tag_name"
          value:
            string_value: "second_tag_value"
  )";
  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest request_proto;
  int64_t timestamp_ns = std::chrono::nanoseconds(timestamp.time_since_epoch()).count();
  TestUtility::loadFromYaml(fmt::format(request_yaml, timestamp_ns, timestamp_ns), request_proto);
  std::string generated_int_hex = Hex::uint64ToHex(generated_int);
  auto* expected_span =
      request_proto.mutable_resource_spans(0)->mutable_scope_spans(0)->mutable_spans(0);
  expected_span->set_trace_id(
      absl::HexStringToBytes(absl::StrCat(generated_int_hex, generated_int_hex)));
  expected_span->set_span_id(absl::HexStringToBytes(absl::StrCat(generated_int_hex)));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.opentelemetry.min_flush_spans", 5U))
      .Times(1)
      .WillRepeatedly(Return(1));
  EXPECT_CALL(*mock_stream_ptr_,
              sendMessageRaw_(Grpc::ProtoBufferEqIgnoreRepeatedFieldOrdering(request_proto), _));
  span->finishSpan();
  EXPECT_EQ(1U, stats_.counter("tracing.opentelemetry.spans_sent").value());
}

// Verifies spans are exported with their attributes and status
TEST_F(OpenTelemetryDriverTest, ExportOTLPSpanWithAttributesAndStatus) {
  setupValidDriver();
  Tracing::TestTraceContextImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};
  NiceMock<Random::MockRandomGenerator>& mock_random_generator_ =
      context_.server_factory_context_.api_.random_;
  int64_t generated_int = 1;
  EXPECT_CALL(mock_random_generator_, random()).Times(3).WillRepeatedly(Return(generated_int));
  SystemTime timestamp = time_system_.systemTime();
  ON_CALL(stream_info_, startTime()).WillByDefault(Return(timestamp));

  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, request_headers, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  EXPECT_NE(span.get(), nullptr);

  span->setTag("first_tag_name", "first_tag_value");
  span->setTag("second_tag_name", "second_tag_value");
  // Try an empty tag.
  span->setTag("", "empty_tag_value");
  // Overwrite a tag.
  span->setTag("first_tag_name", "first_tag_new_value");
  span->setTag("http.status_code", "500");

  // Note the placeholders for the bytes - cleaner to manually set after.
  constexpr absl::string_view request_yaml = R"(
resource_spans:
  resource:
    attributes:
      key: "service.name"
      value:
        string_value: "unknown_service:envoy"
      key: "key1"
      value:
        string_value: "val1"
  scope_spans:
    spans:
      trace_id: "AAA"
      span_id: "AAA"
      name: "test"
      kind: SPAN_KIND_SERVER
      start_time_unix_nano: {}
      end_time_unix_nano: {}
      status:
        code: STATUS_CODE_ERROR
      attributes:
        - key: "first_tag_name"
          value:
            string_value: "first_tag_new_value"
        - key: "second_tag_name"
          value:
            string_value: "second_tag_value"
        - key: "http.status_code"
          value:
            string_value: "500"
  )";
  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest request_proto;
  int64_t timestamp_ns = std::chrono::nanoseconds(timestamp.time_since_epoch()).count();
  TestUtility::loadFromYaml(fmt::format(request_yaml, timestamp_ns, timestamp_ns), request_proto);
  std::string generated_int_hex = Hex::uint64ToHex(generated_int);
  auto* expected_span =
      request_proto.mutable_resource_spans(0)->mutable_scope_spans(0)->mutable_spans(0);
  expected_span->set_trace_id(
      absl::HexStringToBytes(absl::StrCat(generated_int_hex, generated_int_hex)));
  expected_span->set_span_id(absl::HexStringToBytes(absl::StrCat(generated_int_hex)));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.opentelemetry.min_flush_spans", 5U))
      .Times(1)
      .WillRepeatedly(Return(1));
  EXPECT_CALL(*mock_stream_ptr_,
              sendMessageRaw_(Grpc::ProtoBufferEqIgnoreRepeatedFieldOrdering(request_proto), _));
  span->finishSpan();
  EXPECT_EQ(1U, stats_.counter("tracing.opentelemetry.spans_sent").value());
}

// Verifies Grpc spans are exported with their attributes and status
TEST_F(OpenTelemetryDriverTest, ExportOTLPGRPCSpanWithAttributesAndStatus) {
  setupValidDriver();
  Tracing::TestTraceContextImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};
  NiceMock<Random::MockRandomGenerator>& mock_random_generator_ =
      context_.server_factory_context_.api_.random_;
  int64_t generated_int = 1;
  EXPECT_CALL(mock_random_generator_, random()).Times(3).WillRepeatedly(Return(generated_int));
  SystemTime timestamp = time_system_.systemTime();
  ON_CALL(stream_info_, startTime()).WillByDefault(Return(timestamp));

  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, request_headers, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  EXPECT_NE(span.get(), nullptr);

  span->setTag("first_tag_name", "first_tag_value");
  span->setTag("second_tag_name", "second_tag_value");
  // Try an empty tag.
  span->setTag("", "empty_tag_value");
  // Overwrite a tag.
  span->setTag("first_tag_name", "first_tag_new_value");
  span->setTag("http.status_code", "200");
  span->setTag("grpc.status_code", "13");
  span->setTag("grpc.message", "connect Canceled randomly");

  // Note the placeholders for the bytes - cleaner to manually set after.
  constexpr absl::string_view request_yaml = R"(
resource_spans:
  resource:
    attributes:
      key: "service.name"
      value:
        string_value: "unknown_service:envoy"
      key: "key1"
      value:
        string_value: "val1"
  scope_spans:
    spans:
      trace_id: "AAA"
      span_id: "AAA"
      name: "test"
      kind: SPAN_KIND_SERVER
      start_time_unix_nano: {}
      end_time_unix_nano: {}
      status:
        code: STATUS_CODE_ERROR
      attributes:
        - key: "first_tag_name"
          value:
            string_value: "first_tag_new_value"
        - key: "second_tag_name"
          value:
            string_value: "second_tag_value"
        - key: "http.status_code"
          value:
            string_value: "200"
        - key: "grpc.status_code"
          value:
            string_value: "13"
        - key: "grpc.message"
          value:
            string_value: "connect Canceled randomly"
  )";
  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest request_proto;
  int64_t timestamp_ns = std::chrono::nanoseconds(timestamp.time_since_epoch()).count();
  TestUtility::loadFromYaml(fmt::format(request_yaml, timestamp_ns, timestamp_ns), request_proto);
  std::string generated_int_hex = Hex::uint64ToHex(generated_int);
  auto* expected_span =
      request_proto.mutable_resource_spans(0)->mutable_scope_spans(0)->mutable_spans(0);
  expected_span->set_trace_id(
      absl::HexStringToBytes(absl::StrCat(generated_int_hex, generated_int_hex)));
  expected_span->set_span_id(absl::HexStringToBytes(absl::StrCat(generated_int_hex)));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.opentelemetry.min_flush_spans", 5U))
      .Times(1)
      .WillRepeatedly(Return(1));
  EXPECT_CALL(*mock_stream_ptr_,
              sendMessageRaw_(Grpc::ProtoBufferEqIgnoreRepeatedFieldOrdering(request_proto), _));
  span->finishSpan();
  EXPECT_EQ(1U, stats_.counter("tracing.opentelemetry.spans_sent").value());
}

// Not sampled spans are ignored
TEST_F(OpenTelemetryDriverTest, IgnoreNotSampledSpan) {
  setupValidDriver();
  Tracing::TestTraceContextImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};
  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, request_headers, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  EXPECT_NE(span.get(), nullptr);

  span->setSampled(false);

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.opentelemetry.min_flush_spans", 5U)).Times(0);
  EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _)).Times(0);
  span->finishSpan();
  EXPECT_EQ(0U, stats_.counter("tracing.opentelemetry.spans_sent").value());
}

// Verifies tracer is "disabled" when no exporter is configured
TEST_F(OpenTelemetryDriverTest, NoExportWithoutGrpcService) {
  const std::string yaml_string = "{}";
  envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
  TestUtility::loadFromYaml(yaml_string, opentelemetry_config);
  setup(opentelemetry_config);

  Tracing::TestTraceContextImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, request_headers, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  EXPECT_NE(span.get(), nullptr);

  // Flush after a single span.
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.opentelemetry.min_flush_spans", 5U))
      .Times(1)
      .WillRepeatedly(Return(1));
  // We should see a call to sendMessage to export that single span.
  EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _)).Times(0);
  span->finishSpan();
  EXPECT_EQ(0U, stats_.counter("tracing.opentelemetry.spans_sent").value());
}

// Verifies a custom service name is properly set on exported spans
TEST_F(OpenTelemetryDriverTest, ExportSpanWithCustomServiceName) {
  const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    service_name: test-service-name
    )EOF";
  envoy::config::trace::v3::OpenTelemetryConfig opentelemetry_config;
  TestUtility::loadFromYaml(yaml_string, opentelemetry_config);
  setup(opentelemetry_config);

  Tracing::TestTraceContextImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};
  NiceMock<Random::MockRandomGenerator>& mock_random_generator_ =
      context_.server_factory_context_.api_.random_;
  int64_t generated_int = 1;
  EXPECT_CALL(mock_random_generator_, random()).Times(3).WillRepeatedly(Return(generated_int));
  SystemTime timestamp = time_system_.systemTime();
  ON_CALL(stream_info_, startTime()).WillByDefault(Return(timestamp));

  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, request_headers, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  EXPECT_NE(span.get(), nullptr);

  constexpr absl::string_view request_yaml = R"(
resource_spans:
  resource:
    attributes:
      key: "service.name"
      value:
        string_value: "test-service-name"
      key: "key1"
      value:
        string_value: "val1"
  scope_spans:
    spans:
      trace_id: "AAA"
      span_id: "AAA"
      name: "test"
      kind: SPAN_KIND_SERVER
      start_time_unix_nano: {}
      end_time_unix_nano: {}
  )";
  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest request_proto;
  int64_t timestamp_ns = std::chrono::nanoseconds(timestamp.time_since_epoch()).count();
  TestUtility::loadFromYaml(fmt::format(request_yaml, timestamp_ns, timestamp_ns), request_proto);
  std::string generated_int_hex = Hex::uint64ToHex(generated_int);
  auto* expected_span =
      request_proto.mutable_resource_spans(0)->mutable_scope_spans(0)->mutable_spans(0);
  expected_span->set_trace_id(
      absl::HexStringToBytes(absl::StrCat(generated_int_hex, generated_int_hex)));
  expected_span->set_span_id(absl::HexStringToBytes(absl::StrCat(generated_int_hex)));

  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.opentelemetry.min_flush_spans", 5U))
      .Times(1)
      .WillRepeatedly(Return(1));
  EXPECT_CALL(*mock_stream_ptr_,
              sendMessageRaw_(Grpc::ProtoBufferEqIgnoreRepeatedFieldOrdering(request_proto), _));
  span->finishSpan();
  EXPECT_EQ(1U, stats_.counter("tracing.opentelemetry.spans_sent").value());
}

// Verifies the export using the HTTP exporter
TEST_F(OpenTelemetryDriverTest, ExportOTLPSpanHTTP) {
  context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_->name_ =
      "my_o11y_backend";
  context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
      {"my_o11y_backend"});
  ON_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
          httpAsyncClient())
      .WillByDefault(ReturnRef(
          context_.server_factory_context_.cluster_manager_.thread_local_cluster_.async_client_));
  context_.server_factory_context_.cluster_manager_.initializeClusters({"my_o11y_backend"}, {});
  setupValidDriverWithHttpExporter();

  Tracing::TestTraceContextImpl request_headers{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};
  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, request_headers, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  EXPECT_NE(span.get(), nullptr);

  // Flush after a single span.
  EXPECT_CALL(runtime_.snapshot_, getInteger("tracing.opentelemetry.min_flush_spans", 5U))
      .Times(1)
      .WillRepeatedly(Return(1));
  // We should see a call to the async client to export that single span.
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.async_client_,
              send_(_, _, _));

  span->finishSpan();

  EXPECT_EQ(1U, stats_.counter("tracing.opentelemetry.spans_sent").value());
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
