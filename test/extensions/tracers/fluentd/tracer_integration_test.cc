#include "source/extensions/tracers/fluentd/config.h"
#include "source/extensions/tracers/fluentd/fluentd_tracer_impl.h"

#include "test/mocks/server/tracer_factory.h"
#include "test/mocks/server/tracer_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "msgpack.hpp"

using testing::AssertionResult;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Fluentd {

// Tests adapted from OpenTelemetry tracer extension @alexanderellis @yanavlasov
class FluentdTracerIntegrationTest : public testing::Test {
public:
  FluentdTracerIntegrationTest() = default;

  void setup(FluentdConfig& config) {
    ON_CALL(context_.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
        .WillByDefault(testing::Return(&thread_local_cluster_));
    auto client = std::make_unique<NiceMock<Envoy::Tcp::AsyncClient::MockAsyncTcpClient>>();
    ON_CALL(thread_local_cluster_, tcpAsyncClient(_, _))
        .WillByDefault(testing::Return(testing::ByMove(std::move(client))));

    auto config_shared_ptr = std::make_shared<FluentdConfig>(config);

    driver_ =
        std::make_unique<Driver>(config_shared_ptr, context_,
                                 factory.getTracerCacheSingleton(context_.serverFactoryContext()));
  }

  void setupValidDriver() {
    const std::string yaml_json = R"EOF(
      cluster: "fake_cluster"
      tag: "fake_tag"
      stat_prefix: "envoy.tracers.fluentd"
    )EOF";
    FluentdConfig config;
    TestUtility::loadFromYaml(yaml_json, config);

    setup(config);
  }

protected:
  const std::string operation_name_{"test"};
  NiceMock<Server::Configuration::MockTracerFactoryContext> context_;
  NiceMock<Envoy::Tracing::MockConfig> mock_tracing_config_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster_;
  Envoy::Extensions::Tracers::Fluentd::FluentdTracerFactory factory;
  Event::SimulatedTimeSystem time_system_;
  Tracing::DriverSharedPtr driver_;
};

TEST_F(FluentdTracerIntegrationTest, Breathing) {
  setupValidDriver();
  EXPECT_NE(driver_, nullptr);
}

TEST_F(FluentdTracerIntegrationTest, Span) {
  // Set up driver
  setupValidDriver();

  // Create the trace context
  Tracing::TestTraceContextImpl trace_context{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};
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
  trace_context.set(FluentdConstants::get().TRACE_PARENT.key(), parent_trace_header);
  trace_context.set(FluentdConstants::get().TRACE_STATE.key(), "test=foo");

  // Mock the random call for generating span ID so we can check it later.
  const uint64_t new_span_id = 3;
  NiceMock<Random::MockRandomGenerator>& mock_random_generator_ =
      context_.server_factory_context_.api_.random_;
  ON_CALL(mock_random_generator_, random()).WillByDefault(testing::Return(new_span_id));

  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, trace_context, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});

  EXPECT_EQ(span->getTraceId(), trace_id_hex);

  // Test Span functions
  span->setOperation("test_new");
  span->setTag("test_tag", "test_value");
  span->log(SystemTime(std::chrono::seconds(200)), "test_event");

  span->setBaggage("test", "foo");
  auto baggage = span->getBaggage("test");
  EXPECT_EQ(baggage, "");

  span->finishSpan();
}

TEST_F(FluentdTracerIntegrationTest, ParseSpanContextFromHeadersTest) {
  // Set up driver
  setupValidDriver();

  // Create the trace context
  Tracing::TestTraceContextImpl trace_context{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};
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
  trace_context.set(FluentdConstants::get().TRACE_PARENT.key(), parent_trace_header);
  trace_context.set(FluentdConstants::get().TRACE_STATE.key(), "test=foo");

  // Mock the random call for generating span ID so we can check it later.
  const uint64_t new_span_id = 3;
  NiceMock<Random::MockRandomGenerator>& mock_random_generator_ =
      context_.server_factory_context_.api_.random_;
  ON_CALL(mock_random_generator_, random()).WillByDefault(testing::Return(new_span_id));

  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, trace_context, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});

  EXPECT_EQ(span->getTraceId(), trace_id_hex);

  // Remove headers, then inject context into header from the span.
  trace_context.remove(FluentdConstants::get().TRACE_PARENT.key());
  trace_context.remove(FluentdConstants::get().TRACE_STATE.key());
  span->injectContext(trace_context, Tracing::UpstreamContext());

  // Check the headers
  auto sampled_entry = trace_context.get(FluentdConstants::get().TRACE_PARENT.key());
  EXPECT_TRUE(sampled_entry.has_value());
  EXPECT_EQ(
      sampled_entry.value(),
      absl::StrJoin({version, trace_id_hex, Hex::uint64ToHex(new_span_id), trace_flags}, "-"));

  auto sampled_tracestate_entry = trace_context.get(FluentdConstants::get().TRACE_STATE.key());
  EXPECT_TRUE(sampled_tracestate_entry.has_value());
  EXPECT_EQ(sampled_tracestate_entry.value(), "test=foo");
}

TEST_F(FluentdTracerIntegrationTest, GenerateSpanContextWithoutHeadersTest) {
  // Set up driver
  setupValidDriver();

  // Create the trace context
  Tracing::TestTraceContextImpl trace_context{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  // Mock the random call for generating trace and span IDs so we can check it later.
  const uint64_t trace_id_high = 1;
  const uint64_t trace_id_low = 2;
  const uint64_t new_span_id = 3;
  NiceMock<Random::MockRandomGenerator>& mock_random_generator_ =
      context_.server_factory_context_.api_.random_;
  {
    testing::InSequence s;

    EXPECT_CALL(mock_random_generator_, random()).WillOnce(testing::Return(trace_id_high));
    EXPECT_CALL(mock_random_generator_, random()).WillOnce(testing::Return(trace_id_low));
    EXPECT_CALL(mock_random_generator_, random()).WillOnce(testing::Return(new_span_id));
  }

  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, trace_context, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});

  // Remove headers, then inject context into header from the span.
  trace_context.remove(FluentdConstants::get().TRACE_PARENT.key());
  span->injectContext(trace_context, Tracing::UpstreamContext());

  // Check the headers
  auto sampled_entry = trace_context.get(FluentdConstants::get().TRACE_PARENT.key());
  EXPECT_TRUE(sampled_entry.has_value());
  EXPECT_EQ(sampled_entry.value(), "00-00000000000000010000000000000002-0000000000000003-01");
}

TEST_F(FluentdTracerIntegrationTest, NullSpanWithPropagationHeaderError) {
  setupValidDriver();

  // Add an invalid OTLP header to the trace context.
  Tracing::TestTraceContextImpl trace_context{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};
  trace_context.set(FluentdConstants::get().TRACE_PARENT.key(), "invalid00-0000000000000003-01");

  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, trace_context, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});

  auto& null_span = *span;
  EXPECT_EQ(typeid(null_span).name(), typeid(Tracing::NullSpan).name());
}

TEST_F(FluentdTracerIntegrationTest, SpawnChildSpan) {
  // Set up driver
  setupValidDriver();

  // Create the trace context
  Tracing::TestTraceContextImpl trace_context{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  // Mock the random call for generating the parent span's IDs so we can check it later.
  const uint64_t parent_trace_id_high = 0;
  const uint64_t parent_trace_id_low = 2;
  const uint64_t parent_span_id = 3;
  NiceMock<Random::MockRandomGenerator>& mock_random_generator_ =
      context_.server_factory_context_.api_.random_;
  {
    testing::InSequence s;

    EXPECT_CALL(mock_random_generator_, random()).WillOnce(testing::Return(parent_trace_id_high));
    EXPECT_CALL(mock_random_generator_, random()).WillOnce(testing::Return(parent_trace_id_low));
    EXPECT_CALL(mock_random_generator_, random()).WillOnce(testing::Return(parent_span_id));
  }

  Tracing::SpanPtr span = driver_->startSpan(mock_tracing_config_, trace_context, stream_info_,
                                             operation_name_, {Tracing::Reason::Sampling, true});
  EXPECT_NE(span.get(), nullptr);

  // The child should only generate a span ID for itself; the trace id should come from the parent.
  const uint64_t child_span_id = 3;
  EXPECT_CALL(mock_random_generator_, random()).WillOnce(testing::Return(child_span_id));
  Tracing::SpanPtr child_span =
      span->spawnChild(mock_tracing_config_, operation_name_, time_system_.systemTime());

  EXPECT_EQ(child_span->getTraceId(),
            Hex::uint64ToHex(parent_trace_id_high) + Hex::uint64ToHex(parent_trace_id_low));
  EXPECT_EQ(child_span->getSpanId(), Hex::uint64ToHex(child_span_id));
}

} // namespace Fluentd
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
