#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/endpoint/load_report.pb.h"

#include "common/stats/stats_impl.h"
#include "common/upstream/load_stats_reporter.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::_;

// The tests in this file provide just coverage over some corner cases in error handling. The test
// for the happy path for LoadStatsReporter is provided in //test/integration:load_stats_reporter.
namespace Envoy {
namespace Upstream {

class LoadStatsReporterTest : public testing::Test {
public:
  LoadStatsReporterTest()
      : retry_timer_(new Event::MockTimer()), response_timer_(new Event::MockTimer()),
        async_client_(new Grpc::MockAsyncClient()) {
    node_.set_id("foo");
  }

  void createLoadStatsReporter() {
    InSequence s;
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      retry_timer_cb_ = timer_cb;
      return retry_timer_;
    }));
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      response_timer_cb_ = timer_cb;
      return response_timer_;
    }));
    load_stats_reporter_.reset(new LoadStatsReporter(
        node_, cm_, stats_store_, Grpc::AsyncClientPtr(async_client_), dispatcher_));
  }

  void expectSendMessage(
      const std::vector<envoy::api::v2::endpoint::ClusterStats>& expected_cluster_stats) {
    envoy::service::load_stats::v2::LoadStatsRequest expected_request;
    expected_request.mutable_node()->MergeFrom(node_);
    std::copy(expected_cluster_stats.begin(), expected_cluster_stats.end(),
              Protobuf::RepeatedPtrFieldBackInserter(expected_request.mutable_cluster_stats()));
    EXPECT_CALL(async_stream_, sendMessage(ProtoEq(expected_request), false));
  }

  void deliverLoadStatsResponse(const std::vector<std::string>& cluster_names) {
    std::unique_ptr<envoy::service::load_stats::v2::LoadStatsResponse> response(
        new envoy::service::load_stats::v2::LoadStatsResponse());
    response->mutable_load_reporting_interval()->set_seconds(42);
    std::copy(cluster_names.begin(), cluster_names.end(),
              Protobuf::RepeatedPtrFieldBackInserter(response->mutable_clusters()));

    EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000)));
    load_stats_reporter_->onReceiveMessage(std::move(response));
  }

  envoy::api::v2::core::Node node_;
  NiceMock<Upstream::MockClusterManager> cm_;
  Event::MockDispatcher dispatcher_;
  Stats::IsolatedStoreImpl stats_store_;
  std::unique_ptr<LoadStatsReporter> load_stats_reporter_;
  Event::MockTimer* retry_timer_;
  Event::TimerCb retry_timer_cb_;
  Event::MockTimer* response_timer_;
  Event::TimerCb response_timer_cb_;
  Grpc::MockAsyncStream async_stream_;
  Grpc::MockAsyncClient* async_client_;
};

// Validate that stream creation results in a timer based retry.
TEST_F(LoadStatsReporterTest, StreamCreationFailure) {
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(nullptr));
  EXPECT_CALL(*retry_timer_, enableTimer(_));
  createLoadStatsReporter();
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  retry_timer_cb_();
}

TEST_F(LoadStatsReporterTest, TestPubSub) {
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessage(_, _));
  createLoadStatsReporter();
  deliverLoadStatsResponse({"foo"});

  EXPECT_CALL(async_stream_, sendMessage(_, _));
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000)));
  response_timer_cb_();

  EXPECT_CALL(async_stream_, sendMessage(_, _));
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000)));
  response_timer_cb_();

  deliverLoadStatsResponse({"bar"});
}

// Validate that the client can recover from a remote stream closure via retry.
TEST_F(LoadStatsReporterTest, RemoteStreamClose) {
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  EXPECT_CALL(*response_timer_, disableTimer());
  EXPECT_CALL(*retry_timer_, enableTimer(_));
  load_stats_reporter_->onRemoteClose(Grpc::Status::GrpcStatus::Canceled, "");
  EXPECT_CALL(*async_client_, start(_, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  retry_timer_cb_();
}

} // namespace Upstream
} // namespace Envoy
