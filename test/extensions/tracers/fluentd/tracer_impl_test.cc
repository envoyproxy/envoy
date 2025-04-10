#include "envoy/common/time.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/tracers/fluentd/config.h"
#include "source/extensions/tracers/fluentd/fluentd_tracer_impl.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "msgpack.hpp"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Fluentd {

// Tests adapted from test/extensions/access_loggers/fluentd @ohadvano @wbpcode

class FluentdTracerImplTest : public testing::Test {
public:
  FluentdTracerImplTest()
      : async_client_(new Tcp::AsyncClient::MockAsyncTcpClient()),
        backoff_strategy_(new MockBackOffStrategy()),
        flush_timer_(new Event::MockTimer(&dispatcher_)),
        retry_timer_(new Event::MockTimer(&dispatcher_)) {}

  void init(int buffer_size_bytes = 1, absl::optional<int> max_connect_attempts = absl::nullopt) {
    EXPECT_CALL(*async_client_, setAsyncTcpClientCallbacks(_));
    EXPECT_CALL(*flush_timer_, enableTimer(_, _));

    config_.set_tag(tag_);

    if (max_connect_attempts.has_value()) {
      config_.mutable_retry_policy()->mutable_num_retries()->set_value(
          max_connect_attempts.value());
    }

    config_.mutable_buffer_size_bytes()->set_value(buffer_size_bytes);
    tracer_ = std::make_unique<FluentdTracerImpl>(
        cluster_, Tcp::AsyncTcpClientPtr{async_client_}, dispatcher_, config_,
        BackOffStrategyPtr{backoff_strategy_}, *stats_store_.rootScope(), random_);
  }

  std::string getExpectedMsgpackPayload(int entries_count) {
    msgpack::sbuffer buffer;
    msgpack::packer<msgpack::sbuffer> packer(buffer);
    packer.pack_array(3);
    packer.pack(tag_);
    packer.pack_array(entries_count);
    for (int idx = 0; idx < entries_count; idx++) {
      packer.pack_array(2);
      packer.pack(time_);
      packer.pack_map(data_.size());
      for (const auto& pair : data_) {
        packer.pack(pair.first);
        packer.pack(pair.second);
      }
    }

    std::map<std::string, std::string> option_ = {{"fluent_signal", "2"},
                                                  {"TimeFormat", "DateTime"}};
    packer.pack(option_);
    return std::string(buffer.data(), buffer.size());
  }

  std::string tag_ = "test.tag";
  uint64_t time_ = 123;
  std::map<std::string, std::string> data_ = {{"event", "test"}};
  NiceMock<Upstream::MockThreadLocalCluster> cluster_;
  Tcp::AsyncClient::MockAsyncTcpClient* async_client_;
  MockBackOffStrategy* backoff_strategy_;
  Stats::IsolatedStoreImpl stats_store_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* flush_timer_;
  Event::MockTimer* retry_timer_;
  std::unique_ptr<FluentdTracerImpl> tracer_;
  envoy::extensions::tracers::fluentd::v3::FluentdConfig config_;
  NiceMock<Random::MockRandomGenerator> random_;
};

// Fluentd tracer does not write if not connected to upstream.
TEST_F(FluentdTracerImplTest, NoWriteOnTraceIfNotConnectedToUpstream) {
  init();
  EXPECT_CALL(*async_client_, connect()).WillOnce(Return(true));
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  tracer_->log(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
}

// Fluentd tracer does not write if the buffer is not full
TEST_F(FluentdTracerImplTest, NoWriteOnTraceIfBufferLimitNotPassed) {
  init(100);
  EXPECT_CALL(*async_client_, connect()).Times(0);
  EXPECT_CALL(*async_client_, connected()).Times(0);
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  tracer_->log(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
}

// Fluentd tracer does not write if connection is closed remotely
TEST_F(FluentdTracerImplTest, NoWriteOnTraceIfDisconnectedByRemote) {
  init(1, 1);
  EXPECT_CALL(*flush_timer_, disableTimer());
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, connect()).WillOnce(Invoke([this]() -> bool {
    tracer_->onEvent(Network::ConnectionEvent::RemoteClose);
    return true;
  }));

  tracer_->log(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
}

// Fluentd tracer does not write if connection is closed locally
TEST_F(FluentdTracerImplTest, NoWriteOnTraceIfDisconnectedByLocal) {
  init(1, 1);
  EXPECT_CALL(*flush_timer_, disableTimer());
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, connect()).WillOnce(Invoke([this]() -> bool {
    tracer_->onEvent(Network::ConnectionEvent::LocalClose);
    return true;
  }));

  tracer_->log(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
}

// Traces a single entry
TEST_F(FluentdTracerImplTest, TraceSingleEntry) {
  init(); // Default buffer limit is 0 so single entry should be flushed immediately.
  EXPECT_CALL(*backoff_strategy_, reset());
  EXPECT_CALL(*retry_timer_, disableTimer());
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false)).WillOnce(Return(true));
  EXPECT_CALL(*async_client_, connect()).WillOnce(Invoke([this]() -> bool {
    tracer_->onEvent(Network::ConnectionEvent::Connected);
    return true;
  }));
  EXPECT_CALL(*async_client_, write(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool end_stream) {
        EXPECT_FALSE(end_stream);
        std::string expected_payload = getExpectedMsgpackPayload(1);
        EXPECT_EQ(expected_payload, buffer.toString());
      }));

  tracer_->log(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
}

// Traces two entries and flushes them together
TEST_F(FluentdTracerImplTest, TraceTwoEntries) {
  init(12); // First entry is 10 bytes, so first entry should not cause the tracer to flush.

  // First log should not be flushed.
  EXPECT_CALL(*backoff_strategy_, reset());
  EXPECT_CALL(*retry_timer_, disableTimer());
  EXPECT_CALL(*async_client_, connected()).Times(0);
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  tracer_->log(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));

  // Expect second entry to cause all entries to flush.
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false)).WillOnce(Return(true));
  EXPECT_CALL(*async_client_, connect()).WillOnce(Invoke([this]() -> bool {
    tracer_->onEvent(Network::ConnectionEvent::Connected);
    return true;
  }));
  EXPECT_CALL(*async_client_, write(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool end_stream) {
        EXPECT_FALSE(end_stream);
        std::string expected_payload = getExpectedMsgpackPayload(2);
        EXPECT_EQ(expected_payload, buffer.toString());
      }));
  tracer_->log(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
}

// Tests tracer callbacks
TEST_F(FluentdTracerImplTest, CallbacksTest) {
  init();
  EXPECT_CALL(*async_client_, connect()).WillOnce(Return(true));
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  tracer_->log(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
  EXPECT_NO_THROW(tracer_->onAboveWriteBufferHighWatermark());
  EXPECT_NO_THROW(tracer_->onBelowWriteBufferLowWatermark());
  Buffer::OwnedImpl buffer;
  EXPECT_NO_THROW(tracer_->onData(buffer, false));
}

// Fluentd tracer retries connection on failure and holds entry in buffer
TEST_F(FluentdTracerImplTest, SuccessfulReconnect) {
  init(1, 2);
  EXPECT_CALL(*backoff_strategy_, nextBackOffMs()).WillOnce(Return(1));
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false)).WillOnce(Return(true));
  EXPECT_CALL(*async_client_, connect())
      .WillOnce(Invoke([this]() -> bool {
        EXPECT_CALL(*backoff_strategy_, reset()).Times(0);
        EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(1), _));
        EXPECT_CALL(*retry_timer_, disableTimer()).Times(0);
        tracer_->onEvent(Network::ConnectionEvent::LocalClose);
        return true;
      }))
      .WillOnce(Invoke([this]() -> bool {
        EXPECT_CALL(*backoff_strategy_, reset());
        EXPECT_CALL(*retry_timer_, enableTimer(_, _)).Times(0);
        EXPECT_CALL(*retry_timer_, disableTimer());
        tracer_->onEvent(Network::ConnectionEvent::Connected);
        return true;
      }));
  EXPECT_CALL(*async_client_, write(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool end_stream) {
        EXPECT_FALSE(end_stream);
        std::string expected_payload = getExpectedMsgpackPayload(1);
        EXPECT_EQ(expected_payload, buffer.toString());
      }));

  tracer_->log(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
  retry_timer_->invokeCallback();
}

// Fluentd tracer fails to reconnect and does note write the entry
TEST_F(FluentdTracerImplTest, ReconnectFailure) {
  init(1, 2);

  EXPECT_CALL(*backoff_strategy_, nextBackOffMs()).WillOnce(Return(1));
  EXPECT_CALL(*backoff_strategy_, reset()).Times(0);
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(1), _));
  EXPECT_CALL(*retry_timer_, disableTimer()).Times(0);

  EXPECT_CALL(*flush_timer_, disableTimer());
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, connect())
      .WillOnce(Invoke([this]() -> bool {
        tracer_->onEvent(Network::ConnectionEvent::LocalClose);
        return true;
      }))
      .WillOnce(Invoke([this]() -> bool {
        tracer_->onEvent(Network::ConnectionEvent::LocalClose);
        return true;
      }));

  tracer_->log(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
  retry_timer_->invokeCallback();
}

// Fluentd tracer reconnects twice
TEST_F(FluentdTracerImplTest, TwoReconnects) {
  init(1, 3);

  EXPECT_CALL(*backoff_strategy_, nextBackOffMs()).WillOnce(Return(1)).WillOnce(Return(1));
  EXPECT_CALL(*backoff_strategy_, reset()).Times(0);
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(1), _)).Times(2);
  EXPECT_CALL(*retry_timer_, disableTimer()).Times(0);

  EXPECT_CALL(*flush_timer_, disableTimer());
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, connect())
      .WillOnce(Invoke([this]() -> bool {
        tracer_->onEvent(Network::ConnectionEvent::LocalClose);
        return true;
      }))
      .WillOnce(Invoke([this]() -> bool {
        tracer_->onEvent(Network::ConnectionEvent::LocalClose);
        return true;
      }))
      .WillOnce(Invoke([this]() -> bool {
        tracer_->onEvent(Network::ConnectionEvent::LocalClose);
        return true;
      }));

  tracer_->log(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
  retry_timer_->invokeCallback();
  retry_timer_->invokeCallback();
}

// Fluentd tracer retries connection when no healthy upstream is available
TEST_F(FluentdTracerImplTest, RetryOnNoHealthyUpstream) {
  init();

  EXPECT_CALL(*backoff_strategy_, nextBackOffMs()).WillOnce(Return(1));
  EXPECT_CALL(*backoff_strategy_, reset()).Times(0);
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(1), _));
  EXPECT_CALL(*retry_timer_, disableTimer()).Times(0);

  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, connect()).WillOnce(Return(false));
  tracer_->log(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
}

// Fluentd tracer does not write when the buffer limit is reached
TEST_F(FluentdTracerImplTest, NoWriteOnBufferFull) {
  // Setting the buffer to 0 so new log will be thrown.
  init(0);
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  EXPECT_CALL(*async_client_, connect()).Times(0);
  EXPECT_CALL(*async_client_, connected()).Times(0);
  tracer_->log(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
}

TEST_F(FluentdTracerImplTest, TimerFlush) {
  // Set expected flush timer calls
  int buffer_size_bytes = 100;
  EXPECT_CALL(*async_client_, setAsyncTcpClientCallbacks(_));
  EXPECT_CALL(*flush_timer_, enableTimer(_, _)).Times(3);

  config_.set_tag(tag_);

  config_.mutable_buffer_size_bytes()->set_value(buffer_size_bytes);
  tracer_ = std::make_unique<FluentdTracerImpl>(
      cluster_, Tcp::AsyncTcpClientPtr{async_client_}, dispatcher_, config_,
      BackOffStrategyPtr{backoff_strategy_}, *stats_store_.rootScope(), random_);

  // Traced event will be logged on timer flush
  EXPECT_CALL(*backoff_strategy_, reset());
  EXPECT_CALL(*retry_timer_, disableTimer());
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false)).WillOnce(Return(true));
  EXPECT_CALL(*async_client_, connect()).WillOnce(Invoke([this]() -> bool {
    tracer_->onEvent(Network::ConnectionEvent::Connected);
    return true;
  }));
  EXPECT_CALL(*async_client_, write(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool end_stream) {
        EXPECT_FALSE(end_stream);
        std::string expected_payload = getExpectedMsgpackPayload(1);
        EXPECT_EQ(expected_payload, buffer.toString());
      }));

  tracer_->log(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));

  flush_timer_->invokeCallback();

  // No events to log on timer flush
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);

  flush_timer_->invokeCallback();
}

// Testing cache and and tracer creation
class FluentdTracerCacheImplTest : public testing::Test {
public:
  FluentdTracerCacheImplTest() {
    ON_CALL(context.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
        .WillByDefault(testing::Return(&thread_local_cluster));

    auto client = std::make_unique<NiceMock<Envoy::Tcp::AsyncClient::MockAsyncTcpClient>>();
    ON_CALL(thread_local_cluster, tcpAsyncClient(_, _))
        .WillByDefault(testing::Return(testing::ByMove(std::move(client))));

    tracer_cache_ = std::make_unique<FluentdTracerCacheImpl>(
        context.serverFactoryContext().clusterManager(), context.serverFactoryContext().scope(),
        context.serverFactoryContext().threadLocal());
  }

protected:
  // mock cluster manager
  NiceMock<Server::Configuration::MockFactoryContext> context;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  std::unique_ptr<FluentdTracerCacheImpl> tracer_cache_;
};

// Create a tracer with invalid cluster
TEST_F(FluentdTracerCacheImplTest, CreateTracerWhenClusterNotFound) {
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test_cluster")).WillOnce(Return(nullptr));
  tracer_cache_ = std::make_unique<FluentdTracerCacheImpl>(
      cluster_manager_, context.serverFactoryContext().scope(),
      context.serverFactoryContext().threadLocal());

  envoy::extensions::tracers::fluentd::v3::FluentdConfig config;
  config.set_cluster("test_cluster");
  config.set_tag("test.tag");
  config.mutable_buffer_size_bytes()->set_value(123);
  auto tracer = tracer_cache_->getOrCreate(
      std::make_shared<envoy::extensions::tracers::fluentd::v3::FluentdConfig>(config),
      context.serverFactoryContext().api().randomGenerator(), std::unique_ptr<BackOffStrategy>{});
  EXPECT_EQ(tracer, nullptr);
}

// Create a new tracer with valid cluster
TEST_F(FluentdTracerCacheImplTest, CreateNonExistingLogger) {
  envoy::extensions::tracers::fluentd::v3::FluentdConfig config;
  config.set_cluster("test_cluster");
  config.set_tag("test.tag");
  config.mutable_buffer_size_bytes()->set_value(123);
  auto tracer = tracer_cache_->getOrCreate(
      std::make_shared<envoy::extensions::tracers::fluentd::v3::FluentdConfig>(config),
      context.serverFactoryContext().api().randomGenerator(), std::unique_ptr<BackOffStrategy>{});
  EXPECT_NE(tracer, nullptr);
}

// Create a tracer with the same config
TEST_F(FluentdTracerCacheImplTest, CreateTwoTracersSameHash) {
  envoy::extensions::tracers::fluentd::v3::FluentdConfig config;
  config.set_cluster("test_cluster");
  config.set_tag("test.tag");
  config.mutable_buffer_size_bytes()->set_value(123);

  auto tracer1 = tracer_cache_->getOrCreate(
      std::make_shared<envoy::extensions::tracers::fluentd::v3::FluentdConfig>(config),
      context.serverFactoryContext().api().randomGenerator(), std::unique_ptr<BackOffStrategy>{});

  auto tracer2 = tracer_cache_->getOrCreate(
      std::make_shared<envoy::extensions::tracers::fluentd::v3::FluentdConfig>(config),
      context.serverFactoryContext().api().randomGenerator(), std::unique_ptr<BackOffStrategy>{});

  EXPECT_EQ(tracer1, tracer2);
}

// Adapted from OpenTelemetry Span context extractor tests @alexanderellis @yanavlasov
using StatusHelpers::HasStatusMessage;

constexpr absl::string_view version = "00";
constexpr absl::string_view trace_id = "00000000000000000000000000000001";
constexpr absl::string_view parent_id = "0000000000000003";
constexpr absl::string_view trace_flags = "01";

TEST(SpanContextExtractorTest, ExtractSpanContext) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, trace_flags)}};

  SpanContextExtractor span_context_extractor(request_headers);
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_OK(span_context);
  EXPECT_EQ(span_context->traceId(), trace_id);
  EXPECT_EQ(span_context->parentId(), parent_id);
  EXPECT_EQ(span_context->version(), version);
  EXPECT_TRUE(span_context->sampled());
}

TEST(SpanContextExtractorTest, ExtractSpanContextNotSampled) {
  const std::string trace_flags_unsampled{"00"};
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, trace_flags_unsampled)}};
  SpanContextExtractor span_context_extractor(request_headers);
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_OK(span_context);
  EXPECT_EQ(span_context->traceId(), trace_id);
  EXPECT_EQ(span_context->parentId(), parent_id);
  EXPECT_EQ(span_context->version(), version);
  EXPECT_FALSE(span_context->sampled());
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithoutHeader) {
  Tracing::TestTraceContextImpl request_headers{{}};
  SpanContextExtractor span_context_extractor(request_headers);

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("No propagation header found"));
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithTooLongHeader) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("000{}-{}-{}-{}", version, trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("Invalid traceparent header length"));
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithTooShortHeader) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}", trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("Invalid traceparent header length"));
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithInvalidHyphenation) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("{}{}-{}-{}", version, trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("Invalid traceparent header length"));
}

TEST(SpanContextExtractorTest, ThrowExceptionWithInvalidHyphenation) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}---", version, trace_id, parent_id)}};
  SpanContextExtractor span_context_extractor(request_headers);

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("Invalid traceparent hyphenation"));
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithInvalidSizes) {
  const std::string invalid_version{"0"};
  const std::string invalid_trace_flags{"001"};
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", invalid_version, trace_id, parent_id, invalid_trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("Invalid traceparent field sizes"));
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithInvalidHex) {
  const std::string invalid_version{"ZZ"};
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", invalid_version, trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("Invalid header hex"));
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithAllZeroTraceId) {
  const std::string invalid_trace_id{"00000000000000000000000000000000"};
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", version, invalid_trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("Invalid trace id"));
}

TEST(SpanContextExtractorTest, ThrowsExceptionWithAllZeroParentId) {
  const std::string invalid_parent_id{"0000000000000000"};
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent",
       fmt::format("{}-{}-{}-{}", version, trace_id, invalid_parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);

  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("Invalid parent id"));
}

TEST(SpanContextExtractorTest, ExtractSpanContextWithEmptyTracestate) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, trace_flags)}};
  SpanContextExtractor span_context_extractor(request_headers);
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_OK(span_context);
  EXPECT_TRUE(span_context->tracestate().empty());
}

TEST(SpanContextExtractorTest, ExtractSpanContextWithTracestate) {
  Tracing::TestTraceContextImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, trace_flags)},
      {"tracestate", "sample-tracestate"}};
  SpanContextExtractor span_context_extractor(request_headers);
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_OK(span_context);
  EXPECT_EQ(span_context->tracestate(), "sample-tracestate");
}

TEST(SpanContextExtractorTest, IgnoreTracestateWithoutTraceparent) {
  Tracing::TestTraceContextImpl request_headers{{"tracestate", "sample-tracestate"}};
  SpanContextExtractor span_context_extractor(request_headers);
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_FALSE(span_context.ok());
  EXPECT_THAT(span_context, HasStatusMessage("No propagation header found"));
}

TEST(SpanContextExtractorTest, ExtractSpanContextWithMultipleTracestateEntries) {
  Http::TestRequestHeaderMapImpl request_headers{
      {"traceparent", fmt::format("{}-{}-{}-{}", version, trace_id, parent_id, trace_flags)},
      {"tracestate", "sample-tracestate"},
      {"tracestate", "sample-tracestate-2"}};
  Tracing::HttpTraceContext trace_context(request_headers);
  SpanContextExtractor span_context_extractor(trace_context);
  absl::StatusOr<SpanContext> span_context = span_context_extractor.extractSpanContext();

  EXPECT_OK(span_context);
  EXPECT_EQ(span_context->tracestate(), "sample-tracestate,sample-tracestate-2");
}

} // namespace Fluentd
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
