#include "source/extensions/tracers/opentelemetry/opentelemetry_tracer_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/server/tracer_factory_context.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/utility.h"
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
    driver_ = std::make_unique<Driver>(opentelemetry_config, context_);
  }

  void setupValidDriver() {
    const std::string yaml_string = R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: fake-cluster
      timeout: 0.250s
    trace_name: "trace_name"
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
  std::string test_string = "ABCDEFGHIJKLMN";
  Tracing::DriverPtr driver_;
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
  const std::string trace_flags = "00"; //
  const std::vector<std::string> v = {version, trace_id_hex, Hex::uint64ToHex(parent_span_id),
                                      trace_flags};
  const std::string parent_trace_header = absl::StrJoin(v, "-");
  std::cout << "parent_trace_header: " << parent_trace_header << std::endl;

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

    EXPECT_CALL(mock_random_generator_, random())
        .WillOnce(Return(trace_id_high));
    EXPECT_CALL(mock_random_generator_, random())
        .WillOnce(Return(trace_id_low));
    EXPECT_CALL(mock_random_generator_, random())
        .WillOnce(Return(new_span_id));
  }

  Tracing::SpanPtr span =
      driver_->startSpan(mock_tracing_config_, request_headers, operation_name_,
                         time_system_.systemTime(), {Tracing::Reason::Sampling, true});

  // Remove headers, then inject context into header from the span.
  request_headers.remove(OpenTelemetryConstants::get().TRACE_PARENT);
  span->injectContext(request_headers);

  auto sampled_entry = request_headers.get(OpenTelemetryConstants::get().TRACE_PARENT);

  EXPECT_EQ(sampled_entry.size(), 1);
  EXPECT_EQ(sampled_entry[0]->value().getStringView(),
            "00-00000000000000010000000000000002-0000000000000003-00");
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy