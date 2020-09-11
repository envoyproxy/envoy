#include "extensions/tracers/skywalking/tracer.h"

#include "test/extensions/tracers/skywalking/skywalking_test_helper.h"
#include "test/mocks/common.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class TracerTest : public testing::Test {
public:
  void setupTracer(const std::string& yaml_string) {
    EXPECT_CALL(mock_dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      timer_cb_ = timer_cb;
      return timer_;
    }));
    timer_ = new testing::NiceMock<Event::MockTimer>();

    auto client_factory = std::make_unique<testing::NiceMock<Grpc::MockAsyncClientFactory>>();

    auto client = std::make_unique<testing::NiceMock<Grpc::MockAsyncClient>>();
    auto client_ptr = client.get();

    mock_stream_ptr_ = std::make_unique<testing::NiceMock<Grpc::MockAsyncStream>>();

    EXPECT_CALL(*client_factory, create())
        .WillOnce(testing::Return(testing::ByMove(std::move(client))));

    EXPECT_CALL(*client_ptr, startRaw(_, _, _, _))
        .WillOnce(testing::Return(mock_stream_ptr_.get()));

    EXPECT_CALL(mock_cluster_manager_, grpcAsyncClientManager())
        .WillOnce(testing::ReturnRef(mock_async_client_manager_));

    EXPECT_CALL(mock_async_client_manager_, factoryForGrpcService(_, _, _))
        .WillOnce(testing::Return(testing::ByMove(std::move(client_factory))));

    TestUtility::loadFromYaml(yaml_string, config_);
    tracer_ =
        std::make_unique<Tracer>(mock_cluster_manager_, stats_, mock_time_source_, mock_dispatcher_,
                                 config_.grpc_service(), config_.client_config());
  }

protected:
  envoy::config::trace::v3::SkyWalkingConfig config_;

  testing::NiceMock<Envoy::Tracing::MockConfig> mock_tracing_config_;
  testing::NiceMock<Event::MockDispatcher> mock_dispatcher_;
  testing::NiceMock<Random::MockRandomGenerator> mock_random_generator_;
  testing::NiceMock<Envoy::MockTimeSystem> mock_time_source_;
  testing::NiceMock<Envoy::Upstream::MockClusterManager> mock_cluster_manager_;
  testing::NiceMock<Envoy::Grpc::MockAsyncClientManager> mock_async_client_manager_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_;

  std::unique_ptr<testing::NiceMock<Grpc::MockAsyncStream>> mock_stream_ptr_{nullptr};

  testing::NiceMock<Event::MockTimer>* timer_;
  Event::TimerCb timer_cb_;

  TracerPtr tracer_;
};

// Test that the basic functionality of Tracer is working, including creating Span, using Span to
// create new child Spans.
TEST_F(TracerTest, TracerTestCreateNewSpanWithNoPropagationHeaders) {
  setupTracer("{}");
  EXPECT_CALL(mock_random_generator_, random()).WillRepeatedly(testing::Return(666666));
  ON_CALL(mock_time_source_, systemTime())
      .WillByDefault(testing::Return(time_system_.systemTime());

  // Create a new SegmentContext.
  SegmentContextSharedPtr segment_context =
      SkyWalkingTestHelper::createSegmentContext(true, "CURR", "", mock_random_generator_);

  EXPECT_CALL(mock_tracing_config_, operationName())
      .WillOnce(testing::Return(Envoy::Tracing::OperationName::Ingress));
  Envoy::Tracing::SpanPtr org_span = tracer_->startSpan(
      mock_tracing_config_, mock_time_source_.systemTime(), "", segment_context, nullptr);
  Span* span = dynamic_cast<Span*>(org_span.get());

  // Since the operation name in config is Ingress, the new span is entry span.
  EXPECT_EQ(true, span->spanStore()->isEntrySpan());

  // Test whether the basic functions of Span are normal.
  span->setSampled(false);
  EXPECT_EQ(false, span->spanStore()->sampled());

  EXPECT_EQ("CURR#ENDPOINT", span->spanStore()->operation());
  span->setOperation("op");
  EXPECT_EQ("op", span->spanStore()->operation());

  span->setTag("TestTagKeyA", "TestTagValueA");
  span->setTag("TestTagKeyB", "TestTagValueB");
  EXPECT_EQ("TestTagValueA", span->spanStore()->tags().at(0).value());
  EXPECT_EQ("TestTagValueB", span->spanStore()->tags().at(1).value());

  // Entry span does not set the peer address.
  span->setTag(Tracing::Tags::get().PeerAddress, "0.0.0.0");
  EXPECT_EQ("", span->spanStore()->peer());

  // Test the inject context function and verify the result.
  Http::TestRequestHeaderMapImpl headers{{":authority", "test.com"}};
  std::string expected_header_value = fmt::format(
      "{}-{}-{}-{}-{}-{}-{}-{}", 0,
      SkyWalkingTestHelper::base64Encode(SkyWalkingTestHelper::generateId(mock_random_generator_)),
      SkyWalkingTestHelper::base64Encode(SkyWalkingTestHelper::generateId(mock_random_generator_)),
      0, SkyWalkingTestHelper::base64Encode("CURR#SERVICE"),
      SkyWalkingTestHelper::base64Encode("CURR#INSTANCE"),
      SkyWalkingTestHelper::base64Encode("CURR#ENDPOINT"),
      SkyWalkingTestHelper::base64Encode("test.com"));
  span->injectContext(headers);
  EXPECT_EQ(expected_header_value, headers.get_("sw8"));

  EXPECT_CALL(mock_tracing_config_, operationName())
      .WillOnce(testing::Return(Envoy::Tracing::OperationName::Egress));
  Envoy::Tracing::SpanPtr org_first_child_span =
      span->spawnChild(mock_tracing_config_, "TestChild", mock_time_source_.systemTime());
  Span* first_child_span = dynamic_cast<Span*>(org_first_child_span.get());

  // Since the operation name in config is Egress, the new span is exit span.
  EXPECT_EQ(false, first_child_span->spanStore()->isEntrySpan());

  EXPECT_EQ(0, first_child_span->spanStore()->sampled());
  EXPECT_EQ(1, first_child_span->spanStore()->spanId());
  EXPECT_EQ(0, first_child_span->spanStore()->parentSpanId());

  // Exit span will set the peer address.
  first_child_span->setTag(Tracing::Tags::get().PeerAddress, "1.1.1.1");
  EXPECT_EQ("1.1.1.1", first_child_span->spanStore()->peer());
  EXPECT_EQ("TestChild", first_child_span->spanStore()->operation());

  Http::TestRequestHeaderMapImpl first_child_headers{{":authority", "test.com"}};
  expected_header_value = fmt::format(
      "{}-{}-{}-{}-{}-{}-{}-{}", 0,
      SkyWalkingTestHelper::base64Encode(SkyWalkingTestHelper::generateId(mock_random_generator_)),
      SkyWalkingTestHelper::base64Encode(SkyWalkingTestHelper::generateId(mock_random_generator_)),
      1, SkyWalkingTestHelper::base64Encode("CURR#SERVICE"),
      SkyWalkingTestHelper::base64Encode("CURR#INSTANCE"),
      SkyWalkingTestHelper::base64Encode("CURR#ENDPOINT"),
      SkyWalkingTestHelper::base64Encode("1.1.1.1"));
  first_child_span->injectContext(first_child_headers);
  EXPECT_EQ(expected_header_value, first_child_headers.get_("sw8"));

  // Reset sampling flag to true.
  span->setSampled(true);
  EXPECT_CALL(mock_tracing_config_, operationName())
      .WillOnce(testing::Return(Envoy::Tracing::OperationName::Ingress));
  Envoy::Tracing::SpanPtr org_second_child_span =
      span->spawnChild(mock_tracing_config_, "TestChild", mock_time_source_.systemTime());
  Span* second_child_span = dynamic_cast<Span*>(org_second_child_span.get());

  EXPECT_EQ(1, second_child_span->spanStore()->sampled());
  EXPECT_EQ(2, second_child_span->spanStore()->spanId());
  EXPECT_EQ(0, second_child_span->spanStore()->parentSpanId());

  EXPECT_CALL(*mock_stream_ptr_, sendMessageRaw_(_, _)).Times(1);

  // When the child span ends, the data is not reported immediately, but the end time is set.
  first_child_span->finishSpan();
  second_child_span->finishSpan();
  EXPECT_NE(0, first_child_span->spanStore()->endTime());

  // When the first span in the current segment ends, the entire segment is reported.
  span->finishSpan();
}

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
