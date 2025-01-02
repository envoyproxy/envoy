#include "envoy/config/trace/v3/fluentd.pb.h"

#include "source/extensions/tracers/fluentd/fluentd_tracer_impl.h"

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

// Tests adapted from test/extensions/tracers/opentelemetry @alexanderellis @yanavlasov

class FluentdTracerIntegrationTest : public testing::Test {
public:
  FluentdTracerIntegrationTest() {
    cluster_manager_.initializeClusters({"fake_cluster"}, {});
    cluster_manager_.thread_local_cluster_.cluster_.info_->name_ = "fake_cluster";
    cluster_manager_.initializeThreadLocalClusters({"fake_cluster"});

    const std::string yaml_json = R"EOF(
        cluster: "fake_cluster"
        tag: "fake_tag"
        stat_prefix: "envoy.tracers.fluentd"
        buffer_flush_interval: 1s
        buffer_size_bytes: 16384
        retry_options:
            max_connect_attempts: 1024
            backoff_options:
                base_interval: 0.5s
                max_interval: 5s
        )EOF";
    TestUtility::loadFromYaml(yaml_json, config_);
  }

protected:
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_;
  Stats::Scope& scope_{*stats_.rootScope()};
  NiceMock<ThreadLocal::MockInstance> thread_local_slot_allocator_;
  Event::SimulatedTimeSystem time_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Envoy::Tcp::AsyncClient::MockAsyncTcpClient> client_;
  NiceMock<Envoy::Event::MockDispatcher> dispatcher_;
  FluentdConfig config_;
  NiceMock<Envoy::Tracing::MockConfig> mock_tracing_config_;
};

// Test the creation of the FluentdTracerImpl object.
TEST_F(FluentdTracerIntegrationTest, Breathing) {
  auto* cluster = cluster_manager_.getThreadLocalCluster("fake_cluster");

  auto tracer_ = std::make_unique<FluentdTracerImpl>(
      *cluster, std::make_unique<NiceMock<Envoy::Tcp::AsyncClient::MockAsyncTcpClient>>(),
      dispatcher_, config_,
      std::make_unique<JitteredExponentialBackOffStrategy>(1000, 10000, random_), scope_, random_);

  EXPECT_NE(nullptr, tracer_);
}

// Test the creation of a span with a parent span context.
TEST_F(FluentdTracerIntegrationTest, ParseSpanContextFromHeadersTest) {
  // Mock the random call for generating span ID so we can check it later.
  const uint64_t new_span_id = 3;
  ON_CALL(random_, random()).WillByDefault(testing::Return(new_span_id));

  // Make the tracer
  auto* cluster = cluster_manager_.getThreadLocalCluster("fake_cluster");
  auto client = std::make_unique<NiceMock<Envoy::Tcp::AsyncClient::MockAsyncTcpClient>>();
  auto backoff_strategy =
      std::make_unique<JitteredExponentialBackOffStrategy>(1000, 10000, random_);

  const auto tracer_ =
      std::make_shared<FluentdTracerImpl>(*cluster, std::move(client), dispatcher_, config_,
                                          std::move(backoff_strategy), scope_, random_);

  EXPECT_NE(nullptr, tracer_);

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

  // Make the span context
  SpanContext span_context(version, trace_id_hex, Hex::uint64ToHex(parent_span_id), true,
                           "test=foo");

  // Set span info
  Tracing::Decision decision;
  decision.reason = Tracing::Reason::Sampling;
  decision.traced = false;
  const std::string operation_name = "do.thing";
  const SystemTime start = time_.timeSystem().systemTime();
  ON_CALL(stream_info_, startTime()).WillByDefault(testing::Return(start));

  // Make the span
  const auto as_fluentd_tracer = dynamic_cast<FluentdTracerImpl*>(tracer_.get());
  auto span =
      as_fluentd_tracer->startSpan(trace_context, start, operation_name, decision, span_context);

  ASSERT_TRUE(span);
  EXPECT_EQ(span->getTraceId(), trace_id_hex);
  EXPECT_EQ(span->getSpanId(), Hex::uint64ToHex(new_span_id));

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

  // Finish the span
  span->finishSpan();
  EXPECT_EQ(1U, stats_.counter("envoy.tracers.fluentd.entries_buffered").value());
}

// Test the creation of a span without a parent span context.
TEST_F(FluentdTracerIntegrationTest, GenerateSpanContextWithoutHeadersTest) {
  // Mock the random call for generating trace and span IDs so we can check it later.
  const uint64_t trace_id_high = 1;
  const uint64_t trace_id_low = 2;
  const uint64_t new_span_id = 3;
  {
    testing::InSequence s;

    EXPECT_CALL(random_, random()).WillOnce(testing::Return(trace_id_high));
    EXPECT_CALL(random_, random()).WillOnce(testing::Return(trace_id_low));
    EXPECT_CALL(random_, random()).WillOnce(testing::Return(new_span_id));
  }

  // Make the tracer
  auto* cluster = cluster_manager_.getThreadLocalCluster("fake_cluster");
  auto client = std::make_unique<NiceMock<Envoy::Tcp::AsyncClient::MockAsyncTcpClient>>();
  auto backoff_strategy =
      std::make_unique<JitteredExponentialBackOffStrategy>(1000, 10000, random_);

  const auto tracer_ =
      std::make_shared<FluentdTracerImpl>(*cluster, std::move(client), dispatcher_, config_,
                                          std::move(backoff_strategy), scope_, random_);

  EXPECT_NE(nullptr, tracer_);

  // Create the trace context
  Tracing::TestTraceContextImpl trace_context{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  // Set span info
  Tracing::Decision decision = {Tracing::Reason::Sampling, true};
  const std::string operation_name = "do.thing";
  const SystemTime start = time_.timeSystem().systemTime();
  ON_CALL(stream_info_, startTime()).WillByDefault(testing::Return(start));

  // Make the span
  const auto as_fluentd_tracer = dynamic_cast<FluentdTracerImpl*>(tracer_.get());
  auto span = as_fluentd_tracer->startSpan(trace_context, start, operation_name, decision);

  // Remove headers, then inject context into header from the span.
  trace_context.remove(FluentdConstants::get().TRACE_PARENT.key());
  span->injectContext(trace_context, Tracing::UpstreamContext());

  // Check the headers
  auto sampled_entry = trace_context.get(FluentdConstants::get().TRACE_PARENT.key());

  EXPECT_TRUE(sampled_entry.has_value());
  EXPECT_EQ(sampled_entry.value(), "00-00000000000000010000000000000002-0000000000000003-01");
}

// Test that an invalid trace context will be flagged by the span context extractor.
TEST_F(FluentdTracerIntegrationTest, NullSpanWithPropagationHeaderError) {
  // Add an invalid OTLP header to the request headers.
  Tracing::TestTraceContextImpl trace_context{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};
  trace_context.set(FluentdConstants::get().TRACE_PARENT.key(), "invalid00-0000000000000003-01");

  SpanContextExtractor extractor(trace_context);

  EXPECT_TRUE(extractor.propagationHeaderPresent());
  EXPECT_FALSE(extractor.extractSpanContext().ok());
}

// Test the creation of a child span
TEST_F(FluentdTracerIntegrationTest, SpawnChildSpan) {
  // Mock the random call for generating trace and span IDs so we can check it later.
  const uint64_t trace_id_high = 0;
  const uint64_t trace_id_low = 2;
  const uint64_t new_span_id = 3;
  {
    testing::InSequence s;

    EXPECT_CALL(random_, random()).WillOnce(testing::Return(trace_id_high));
    EXPECT_CALL(random_, random()).WillOnce(testing::Return(trace_id_low));
    EXPECT_CALL(random_, random()).WillOnce(testing::Return(new_span_id));
  }
  // Make the tracer
  auto* cluster = cluster_manager_.getThreadLocalCluster("fake_cluster");
  auto client = std::make_unique<NiceMock<Envoy::Tcp::AsyncClient::MockAsyncTcpClient>>();
  auto backoff_strategy =
      std::make_unique<JitteredExponentialBackOffStrategy>(1000, 10000, random_);

  const auto tracer_ =
      std::make_shared<FluentdTracerImpl>(*cluster, std::move(client), dispatcher_, config_,
                                          std::move(backoff_strategy), scope_, random_);

  EXPECT_NE(nullptr, tracer_);

  // Create the trace context
  Tracing::TestTraceContextImpl trace_context{
      {":authority", "test.com"}, {":path", "/"}, {":method", "GET"}};

  // Set span info
  Tracing::Decision decision = {Tracing::Reason::Sampling, true};
  const std::string operation_name = "do.thing";
  const SystemTime start = time_.timeSystem().systemTime();
  ON_CALL(stream_info_, startTime()).WillByDefault(testing::Return(start));

  // Make the span
  const auto as_fluentd_tracer = dynamic_cast<FluentdTracerImpl*>(tracer_.get());
  auto span = as_fluentd_tracer->startSpan(trace_context, start, operation_name, decision);

  EXPECT_NE(span.get(), nullptr);
  EXPECT_EQ(span->getTraceId(), Hex::uint64ToHex(trace_id_high) + Hex::uint64ToHex(trace_id_low));
  EXPECT_EQ(span->getSpanId(), Hex::uint64ToHex(new_span_id));

  // The child should only generate a span ID for itself; the trace id should come from the parent.
  const uint64_t child_span_id = 3;
  EXPECT_CALL(random_, random()).WillOnce(testing::Return(child_span_id));
  auto child_span =
      span->spawnChild(mock_tracing_config_, operation_name, time_.timeSystem().systemTime());

  EXPECT_EQ(child_span->getTraceId(),
            Hex::uint64ToHex(trace_id_high) + Hex::uint64ToHex(trace_id_low));
  EXPECT_EQ(child_span->getSpanId(), Hex::uint64ToHex(child_span_id));
}

} // namespace Fluentd
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
