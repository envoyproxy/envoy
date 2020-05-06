#include <memory>

#include "envoy/config/endpoint/v3/load_report.pb.h"
#include "envoy/service/load_stats/v3/lrs.pb.h"

#include "common/upstream/load_stats_reporter.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

// The tests in this file provide just coverage over some corner cases in error handling. The test
// for the happy path for LoadStatsReporter is provided in //test/integration:load_stats_reporter.
namespace Envoy {
namespace Upstream {
namespace {

class LoadStatsReporterTest : public testing::Test {
public:
  LoadStatsReporterTest()
      : retry_timer_(new Event::MockTimer()), response_timer_(new Event::MockTimer()),
        async_client_(new Grpc::MockAsyncClient()) {}

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
    load_stats_reporter_ = std::make_unique<LoadStatsReporter>(
        local_info_, cm_, stats_store_, Grpc::RawAsyncClientPtr(async_client_),
        envoy::config::core::v3::ApiVersion::AUTO, dispatcher_);
  }

  void expectSendMessage(
      const std::vector<envoy::config::endpoint::v3::ClusterStats>& expected_cluster_stats) {
    envoy::service::load_stats::v3::LoadStatsRequest expected_request;
    expected_request.mutable_node()->MergeFrom(local_info_.node());
    expected_request.mutable_node()->add_client_features("envoy.lrs.supports_send_all_clusters");
    std::copy(expected_cluster_stats.begin(), expected_cluster_stats.end(),
              Protobuf::RepeatedPtrFieldBackInserter(expected_request.mutable_cluster_stats()));
    EXPECT_CALL(async_stream_, sendMessageRaw_(Grpc::ProtoBufferEq(expected_request), false));
  }

  void deliverLoadStatsResponse(const std::vector<std::string>& cluster_names) {
    std::unique_ptr<envoy::service::load_stats::v3::LoadStatsResponse> response(
        new envoy::service::load_stats::v3::LoadStatsResponse());
    response->mutable_load_reporting_interval()->set_seconds(42);
    std::copy(cluster_names.begin(), cluster_names.end(),
              Protobuf::RepeatedPtrFieldBackInserter(response->mutable_clusters()));

    EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
    load_stats_reporter_->onReceiveMessage(std::move(response));
  }

  Event::SimulatedTimeSystem time_system_;
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
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
};

// Validate that stream creation results in a timer based retry.
TEST_F(LoadStatsReporterTest, StreamCreationFailure) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(nullptr));
  EXPECT_CALL(*retry_timer_, enableTimer(_, _));
  createLoadStatsReporter();
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  retry_timer_cb_();
}

TEST_F(LoadStatsReporterTest, TestPubSub) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  createLoadStatsReporter();
  deliverLoadStatsResponse({"foo"});

  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();

  deliverLoadStatsResponse({"bar"});

  EXPECT_CALL(async_stream_, sendMessageRaw_(_, _));
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();
}

// Validate treatment of existing clusters across updates.
TEST_F(LoadStatsReporterTest, ExistingClusters) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  // Initially, we have no clusters to report on.
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(3));
  // Start reporting on foo.
  NiceMock<MockClusterMockPrioritySet> foo_cluster;
  foo_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(2);
  foo_cluster.info_->eds_service_name_ = "bar";
  NiceMock<MockClusterMockPrioritySet> bar_cluster;
  MockClusterManager::ClusterInfoMap cluster_info{{"foo", foo_cluster}, {"bar", bar_cluster}};
  ON_CALL(cm_, clusters()).WillByDefault(Return(cluster_info));
  deliverLoadStatsResponse({"foo"});
  // Initial stats report for foo on timer tick.
  foo_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(5);
  time_system_.setMonotonicTime(std::chrono::microseconds(4));
  {
    envoy::config::endpoint::v3::ClusterStats foo_cluster_stats;
    foo_cluster_stats.set_cluster_name("foo");
    foo_cluster_stats.set_cluster_service_name("bar");
    foo_cluster_stats.set_total_dropped_requests(5);
    foo_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(1));
    expectSendMessage({foo_cluster_stats});
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();

  // Some traffic on foo/bar in between previous request and next response.
  foo_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);
  bar_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);

  // Start reporting on bar.
  time_system_.setMonotonicTime(std::chrono::microseconds(6));
  deliverLoadStatsResponse({"foo", "bar"});
  // Stats report foo/bar on timer tick.
  foo_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);
  bar_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);
  time_system_.setMonotonicTime(std::chrono::microseconds(28));
  {
    envoy::config::endpoint::v3::ClusterStats foo_cluster_stats;
    foo_cluster_stats.set_cluster_name("foo");
    foo_cluster_stats.set_cluster_service_name("bar");
    foo_cluster_stats.set_total_dropped_requests(2);
    foo_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(24));
    envoy::config::endpoint::v3::ClusterStats bar_cluster_stats;
    bar_cluster_stats.set_cluster_name("bar");
    bar_cluster_stats.set_total_dropped_requests(1);
    bar_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(22));
    expectSendMessage({bar_cluster_stats, foo_cluster_stats});
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();

  // Some traffic on foo/bar in between previous request and next response.
  foo_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);
  bar_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);

  // Stop reporting on foo.
  deliverLoadStatsResponse({"bar"});
  // Stats report for bar on timer tick.
  foo_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(5);
  bar_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(5);
  time_system_.setMonotonicTime(std::chrono::microseconds(33));
  {
    envoy::config::endpoint::v3::ClusterStats bar_cluster_stats;
    bar_cluster_stats.set_cluster_name("bar");
    bar_cluster_stats.set_total_dropped_requests(6);
    bar_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(5));
    expectSendMessage({bar_cluster_stats});
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();

  // Some traffic on foo/bar in between previous request and next response.
  foo_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);
  bar_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);

  // Start tracking foo again, we should forget earlier history for foo.
  time_system_.setMonotonicTime(std::chrono::microseconds(43));
  deliverLoadStatsResponse({"foo", "bar"});
  // Stats report foo/bar on timer tick.
  foo_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);
  bar_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);
  time_system_.setMonotonicTime(std::chrono::microseconds(47));
  {
    envoy::config::endpoint::v3::ClusterStats foo_cluster_stats;
    foo_cluster_stats.set_cluster_name("foo");
    foo_cluster_stats.set_cluster_service_name("bar");
    foo_cluster_stats.set_total_dropped_requests(1);
    foo_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(4));
    envoy::config::endpoint::v3::ClusterStats bar_cluster_stats;
    bar_cluster_stats.set_cluster_name("bar");
    bar_cluster_stats.set_total_dropped_requests(2);
    bar_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(14));
    expectSendMessage({bar_cluster_stats, foo_cluster_stats});
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();
}

// Validate that the client can recover from a remote stream closure via retry.
TEST_F(LoadStatsReporterTest, RemoteStreamClose) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  EXPECT_CALL(*response_timer_, disableTimer());
  EXPECT_CALL(*retry_timer_, enableTimer(_, _));
  load_stats_reporter_->onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Canceled, "");
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  retry_timer_cb_();
}

} // namespace
} // namespace Upstream
} // namespace Envoy
