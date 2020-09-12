#include "extensions/tracers/skywalking/trace_segment_reporter.h"

#include "test/extensions/tracers/skywalking/skywalking_test_helper.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class TraceSegmentReporterTest : public testing::Test {
public:
  TraceSegmentReporterTest()
      : tracing_stats_{
            SKYWALKING_TRACER_STATS(POOL_COUNTER_PREFIX(mock_scope_, "tracing.skywalking."))} {}

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

    EXPECT_CALL(*mock_client_factory, create()).WillOnce(Return(ByMove(std::move(mock_client))));
    EXPECT_CALL(*mock_client_ptr_, startRaw(_, _, _, _)).WillOnce(Return(mock_stream_ptr_.get()));

    TestUtility::loadFromYaml(yaml_string, client_config_);

    reporter_ = std::make_unique<TraceSegmentReporter>(
        std::move(mock_client_factory), mock_dispatcher_, tracing_stats_, client_config_);
  }

protected:
  NiceMock<Event::MockDispatcher> mock_dispatcher_;
  NiceMock<Random::MockRandomGenerator> mock_random_generator_;
  NiceMock<Envoy::MockTimeSystem> mock_time_source_;
  NiceMock<Stats::MockIsolatedStatsStore> mock_scope_;

  NiceMock<Grpc::MockAsyncClient>* mock_client_ptr_{nullptr};

  std::unique_ptr<NiceMock<Grpc::MockAsyncStream>> mock_stream_ptr_{nullptr};

  NiceMock<Event::MockTimer>* timer_;
  Event::TimerCb timer_cb_;

  envoy::config::trace::v3::ClientConfig client_config_;
  SkyWalkingTracerStats tracing_stats_;
  TraceSegmentReporterPtr reporter_;
};

TEST_F(TraceSegmentReporterTest, TraceSegmentReporterReportTraceSegment) {
  setupTraceSegmentReporter("{}");
  Event::SimulatedTimeSystem time_system;
  ON_CALL(mock_random_generator_, random()).WillByDefault(Return(23333));
  ON_CALL(mock_time_source_, systemTime()).WillByDefault(Return(time_system.systemTime()));
  SegmentContextSharedPtr segment_context =
      SkyWalkingTestHelper::createSegmentContext(true, "NEW", "PRE", mock_random_generator_);
  SpanStore* parent_store = SkyWalkingTestHelper::createSpanStore(segment_context.get(), nullptr,
                                                                  "PARENT", mock_time_source_);
  SkyWalkingTestHelper::createSpanStore(segment_context.get(), parent_store, "CHILD",
                                        mock_time_source_);

  EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _));

  reporter_->report(*segment_context);
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
