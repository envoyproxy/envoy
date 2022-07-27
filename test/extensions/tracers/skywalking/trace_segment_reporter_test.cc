#include "source/extensions/tracers/skywalking/trace_segment_reporter.h"

#include "test/extensions/tracers/skywalking/skywalking_test_helper.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/server/tracer_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {
namespace {

class TraceSegmentReporterTest : public testing::Test {
public:
  void setupTraceSegmentReporter(const std::string& yaml_string) {
    EXPECT_CALL(mock_dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      timer_cb_ = timer_cb;
      return timer_;
    }));
    timer_ = new NiceMock<Event::MockTimer>();

    auto mock_client_factory = std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();

    auto mock_client = std::make_unique<NiceMock<Grpc::MockAsyncClient>>();
    mock_client_ptr_ = mock_client.get();

    mock_stream_ptr_ = std::make_unique<NiceMock<Grpc::MockAsyncStream>>();

    EXPECT_CALL(*mock_client_factory, createUncachedRawAsyncClient())
        .WillOnce(Return(ByMove(std::move(mock_client))));
    EXPECT_CALL(*mock_client_ptr_, startRaw(_, _, _, _)).WillOnce(Return(mock_stream_ptr_.get()));

    auto& local_info = context_.server_factory_context_.local_info_;

    ON_CALL(local_info, clusterName()).WillByDefault(ReturnRef(test_string));
    ON_CALL(local_info, nodeName()).WillByDefault(ReturnRef(test_string));

    envoy::config::trace::v3::ClientConfig proto_client_config;
    TestUtility::loadFromYaml(yaml_string, proto_client_config);

    reporter_ = std::make_unique<TraceSegmentReporter>(
        std::move(mock_client_factory), mock_dispatcher_, mock_random_generator_, tracing_stats_,
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_client_config, max_cache_size, 1024),
        proto_client_config.backend_token());
  }

protected:
  NiceMock<Envoy::Server::Configuration::MockTracerFactoryContext> context_;
  NiceMock<Event::MockDispatcher>& mock_dispatcher_ = context_.server_factory_context_.dispatcher_;
  NiceMock<Random::MockRandomGenerator>& mock_random_generator_ =
      context_.server_factory_context_.api_.random_;
  Event::GlobalTimeSystem& mock_time_source_ = context_.server_factory_context_.time_system_;
  NiceMock<Stats::MockIsolatedStatsStore>& mock_scope_ = context_.server_factory_context_.scope_;
  NiceMock<Grpc::MockAsyncClient>* mock_client_ptr_{nullptr};
  std::unique_ptr<NiceMock<Grpc::MockAsyncStream>> mock_stream_ptr_{nullptr};
  NiceMock<Event::MockTimer>* timer_;
  Event::TimerCb timer_cb_;
  std::string test_string = "ABCDEFGHIJKLMN";
  SkyWalkingTracerStatsSharedPtr tracing_stats_{
      std::make_shared<SkyWalkingTracerStats>(SkyWalkingTracerStats{
          SKYWALKING_TRACER_STATS(POOL_COUNTER_PREFIX(mock_scope_, "tracing.skywalking."))})};
  TraceSegmentReporterPtr reporter_;
};

// Test whether the reporter can correctly add metadata according to the configuration.
TEST_F(TraceSegmentReporterTest, TraceSegmentReporterInitialMetadata) {
  const std::string yaml_string = R"EOF(
    backend_token: "FakeStringForAuthenticaion"
  )EOF";

  setupTraceSegmentReporter(yaml_string);
  Http::TestRequestHeaderMapImpl metadata;
  reporter_->onCreateInitialMetadata(metadata);

  EXPECT_EQ("FakeStringForAuthenticaion", metadata.get_("authentication"));
}

TEST_F(TraceSegmentReporterTest, TraceSegmentReporterNoMetadata) {
  setupTraceSegmentReporter("{}");
  Http::TestRequestHeaderMapImpl metadata;
  reporter_->onCreateInitialMetadata(metadata);

  EXPECT_EQ("", metadata.get_("authentication"));
}

TEST_F(TraceSegmentReporterTest, TraceSegmentReporterReportTraceSegment) {
  setupTraceSegmentReporter("{}");
  ON_CALL(mock_random_generator_, random()).WillByDefault(Return(23333));

  TracingContextPtr segment_context =
      SkyWalkingTestHelper::createSegmentContext(true, "NEW", "PRE");
  TracingSpanPtr parent_store =
      SkyWalkingTestHelper::createSpanStore(segment_context, nullptr, "PARENT");

  // Skip reporting the first child span.
  TracingSpanPtr first_child_sptore =
      SkyWalkingTestHelper::createSpanStore(segment_context, parent_store, "CHILD", false);

  // Create second child span.
  SkyWalkingTestHelper::createSpanStore(segment_context, parent_store, "CHILD");

  EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _));

  reporter_->report(segment_context);

  EXPECT_EQ(1U, mock_scope_.counter("tracing.skywalking.segments_sent").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_dropped").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.cache_flushed").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_flushed").value());

  // Create a segment context with no previous span context.
  TracingContextPtr second_segment_context =
      SkyWalkingTestHelper::createSegmentContext(true, "SECOND_SEGMENT", "");
  SkyWalkingTestHelper::createSpanStore(second_segment_context, nullptr, "PARENT");

  EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _));
  reporter_->report(second_segment_context);

  EXPECT_EQ(2U, mock_scope_.counter("tracing.skywalking.segments_sent").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_dropped").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.cache_flushed").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_flushed").value());
}

TEST_F(TraceSegmentReporterTest, TraceSegmentReporterReportWithDefaultCache) {
  setupTraceSegmentReporter("{}");
  ON_CALL(mock_random_generator_, random()).WillByDefault(Return(23333));

  TracingContextPtr segment_context =
      SkyWalkingTestHelper::createSegmentContext(true, "NEW", "PRE");
  TracingSpanPtr parent_store =
      SkyWalkingTestHelper::createSpanStore(segment_context, nullptr, "PARENT");
  SkyWalkingTestHelper::createSpanStore(segment_context, parent_store, "CHILD");

  EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _)).Times(1025);

  reporter_->report(segment_context);

  EXPECT_EQ(1U, mock_scope_.counter("tracing.skywalking.segments_sent").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_dropped").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.cache_flushed").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_flushed").value());

  // Simulates a disconnected connection.
  EXPECT_CALL(*timer_, enableTimer(_, _));
  reporter_->onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Unknown, "");

  // Try to report 10 segments. Due to the disconnection, the cache size is only 3. So 7 of the
  // segments will be discarded.
  for (int i = 0; i < 2048; i++) {
    reporter_->report(segment_context);
  }

  EXPECT_EQ(1U, mock_scope_.counter("tracing.skywalking.segments_sent").value());
  EXPECT_EQ(1024U, mock_scope_.counter("tracing.skywalking.segments_dropped").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.cache_flushed").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_flushed").value());

  // Simulate the situation where the connection is re-established. The remaining segments in the
  // cache will be reported.
  EXPECT_CALL(*mock_client_ptr_, startRaw(_, _, _, _)).WillOnce(Return(mock_stream_ptr_.get()));
  timer_cb_();

  EXPECT_EQ(1025U, mock_scope_.counter("tracing.skywalking.segments_sent").value());
  EXPECT_EQ(1024U, mock_scope_.counter("tracing.skywalking.segments_dropped").value());
  EXPECT_EQ(1U, mock_scope_.counter("tracing.skywalking.cache_flushed").value());
  EXPECT_EQ(1024U, mock_scope_.counter("tracing.skywalking.segments_flushed").value());
}

TEST_F(TraceSegmentReporterTest, TraceSegmentReporterReportWithCacheConfig) {
  const std::string yaml_string = R"EOF(
    max_cache_size: 3
  )EOF";

  setupTraceSegmentReporter(yaml_string);

  ON_CALL(mock_random_generator_, random()).WillByDefault(Return(23333));

  TracingContextPtr segment_context =
      SkyWalkingTestHelper::createSegmentContext(true, "NEW", "PRE");
  TracingSpanPtr parent_store =
      SkyWalkingTestHelper::createSpanStore(segment_context, nullptr, "PARENT");
  SkyWalkingTestHelper::createSpanStore(segment_context, parent_store, "CHILD");

  EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _)).Times(4);

  reporter_->report(segment_context);

  EXPECT_EQ(1U, mock_scope_.counter("tracing.skywalking.segments_sent").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_dropped").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.cache_flushed").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_flushed").value());

  // Simulates a disconnected connection.
  EXPECT_CALL(*timer_, enableTimer(_, _));
  reporter_->onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Unknown, "");

  // Try to report 10 segments. Due to the disconnection, the cache size is only 3. So 7 of the
  // segments will be discarded.
  for (int i = 0; i < 10; i++) {
    reporter_->report(segment_context);
  }

  EXPECT_EQ(1U, mock_scope_.counter("tracing.skywalking.segments_sent").value());
  EXPECT_EQ(7U, mock_scope_.counter("tracing.skywalking.segments_dropped").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.cache_flushed").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.skywalking.segments_flushed").value());

  // Simulate the situation where the connection is re-established. The remaining segments in the
  // cache will be reported.
  EXPECT_CALL(*mock_client_ptr_, startRaw(_, _, _, _)).WillOnce(Return(mock_stream_ptr_.get()));
  timer_cb_();

  EXPECT_EQ(4U, mock_scope_.counter("tracing.skywalking.segments_sent").value());
  EXPECT_EQ(7U, mock_scope_.counter("tracing.skywalking.segments_dropped").value());
  EXPECT_EQ(1U, mock_scope_.counter("tracing.skywalking.cache_flushed").value());
  EXPECT_EQ(3U, mock_scope_.counter("tracing.skywalking.segments_flushed").value());
}

TEST_F(TraceSegmentReporterTest, CallAsyncCallbackAndNothingTodo) {
  setupTraceSegmentReporter("{}");
  reporter_->onReceiveInitialMetadata(std::make_unique<Http::TestResponseHeaderMapImpl>());
  reporter_->onReceiveTrailingMetadata(std::make_unique<Http::TestResponseTrailerMapImpl>());
  reporter_->onReceiveMessage(std::make_unique<skywalking::v3::Commands>());
}

} // namespace
} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
