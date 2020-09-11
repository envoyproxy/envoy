#include "extensions/tracers/skywalking/trace_segment_reporter.h"

#include "test/extensions/tracers/skywalking/skywalking_test_helper.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

// TODO(wbpcode): we need stats to get internal status.

class TraceSegmentReporterTest : public testing::Test {
public:
  void setupTraceSegmentReporter(const std::string& yaml_string) {
    EXPECT_CALL(mock_dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      timer_cb_ = timer_cb;
      return timer_;
    }));
    timer_ = new testing::NiceMock<Event::MockTimer>();

    auto client_factory = std::make_unique<testing::NiceMock<Grpc::MockAsyncClientFactory>>();

    auto client = std::make_unique<testing::NiceMock<Grpc::MockAsyncClient>>();
    mock_client_ptr_ = client.get();

    mock_stream_ptr_ = std::make_unique<testing::NiceMock<Grpc::MockAsyncStream>>();

    EXPECT_CALL(*client_factory, create())
        .WillOnce(testing::Return(testing::ByMove(std::move(client))));
    EXPECT_CALL(*mock_client_ptr_, startRaw(_, _, _, _))
        .WillOnce(testing::Return(mock_stream_ptr_.get()));

    TestUtility::loadFromYaml(yaml_string, client_config_);

    reporter_ = std::make_unique<TraceSegmentReporter>(std::move(client_factory), mock_dispatcher_,
                                                       client_config_);
  }

protected:
  envoy::config::trace::v3::ClientConfig client_config_;

  testing::NiceMock<Event::MockDispatcher> mock_dispatcher_;
  testing::NiceMock<Random::MockRandomGenerator> mock_random_generator_;
  testing::NiceMock<Envoy::MockTimeSystem> mock_time_source_;

  testing::NiceMock<Grpc::MockAsyncClient>* mock_client_ptr_{nullptr};

  std::unique_ptr<testing::NiceMock<Grpc::MockAsyncStream>> mock_stream_ptr_{nullptr};

  testing::NiceMock<Event::MockTimer>* timer_;
  Event::TimerCb timer_cb_;

  TraceSegmentReporterPtr reporter_;
};

TEST_F(TraceSegmentReporterTest, TraceSegmentReporterReportTraceSegment) {
  setupTraceSegmentReporter("{}");

  ON_CALL(mock_random_generator_, random()).WillByDefault(testing::Return(23333));
  ON_CALL(mock_time_source_, systemTime())
      .WillByDefault(testing::Return(time_system_.systemTime()));
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
