#include <memory>

#include "envoy/config/endpoint/v3/load_report.pb.h"
#include "envoy/service/load_stats/v3/lrs.pb.h"

#include "source/common/upstream/load_stats_reporter.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_priority_set.h"
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
    load_stats_reporter_ =
        std::make_unique<LoadStatsReporter>(local_info_, cm_, *stats_store_.rootScope(),
                                            Grpc::RawAsyncClientPtr(async_client_), dispatcher_);
  }

  void expectSendMessage(
      const std::vector<envoy::config::endpoint::v3::ClusterStats>& expected_cluster_stats) {
    envoy::service::load_stats::v3::LoadStatsRequest expected_request;
    expected_request.mutable_node()->MergeFrom(local_info_.node());
    expected_request.mutable_node()->add_client_features("envoy.lrs.supports_send_all_clusters");
    std::copy(expected_cluster_stats.begin(), expected_cluster_stats.end(),
              Protobuf::RepeatedPtrFieldBackInserter(expected_request.mutable_cluster_stats()));
    EXPECT_CALL(
        async_stream_,
        sendMessageRaw_(Grpc::ProtoBufferEqIgnoreRepeatedFieldOrdering(expected_request), false));
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

  void setDropOverload(envoy::config::endpoint::v3::ClusterStats& cluster_stats, uint64_t count) {
    auto* dropped_request = cluster_stats.add_dropped_requests();
    dropped_request->set_category("drop_overload");
    dropped_request->set_dropped_count(count);
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
  MockClusterManager::ClusterInfoMaps cluster_info{
      {{"foo", foo_cluster}, {"bar", bar_cluster}}, {}, {}};
  ON_CALL(cm_, clusters()).WillByDefault(Return(cluster_info));
  deliverLoadStatsResponse({"foo"});
  // Initial stats report for foo on timer tick.
  foo_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(5);
  foo_cluster.info_->load_report_stats_.upstream_rq_drop_overload_.add(7);
  time_system_.setMonotonicTime(std::chrono::microseconds(4));
  {
    envoy::config::endpoint::v3::ClusterStats foo_cluster_stats;
    foo_cluster_stats.set_cluster_name("foo");
    foo_cluster_stats.set_cluster_service_name("bar");
    foo_cluster_stats.set_total_dropped_requests(5);
    setDropOverload(foo_cluster_stats, 7);
    foo_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(1));
    expectSendMessage({foo_cluster_stats});
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();

  // Some traffic on foo/bar in between previous request and next response.
  foo_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);
  bar_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);
  bar_cluster.info_->load_report_stats_.upstream_rq_drop_overload_.add(5);

  // Start reporting on bar.
  time_system_.setMonotonicTime(std::chrono::microseconds(6));
  deliverLoadStatsResponse({"foo", "bar"});
  // Stats report foo/bar on timer tick.
  foo_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);
  bar_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);
  bar_cluster.info_->load_report_stats_.upstream_rq_drop_overload_.add(3);
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
    setDropOverload(bar_cluster_stats, 3);
    bar_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(22));
    expectSendMessage({bar_cluster_stats, foo_cluster_stats});
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();

  // Some traffic on foo/bar in between previous request and next response.
  foo_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);
  bar_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);
  bar_cluster.info_->load_report_stats_.upstream_rq_drop_overload_.add(1);

  // Stop reporting on foo.
  deliverLoadStatsResponse({"bar"});
  // Stats report for bar on timer tick.
  foo_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(5);
  bar_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(5);
  bar_cluster.info_->load_report_stats_.upstream_rq_drop_overload_.add(7);
  time_system_.setMonotonicTime(std::chrono::microseconds(33));
  {
    envoy::config::endpoint::v3::ClusterStats bar_cluster_stats;
    bar_cluster_stats.set_cluster_name("bar");
    bar_cluster_stats.set_total_dropped_requests(6);
    setDropOverload(bar_cluster_stats, 8);
    bar_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(5));
    expectSendMessage({bar_cluster_stats});
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();

  // Some traffic on foo/bar in between previous request and next response.
  foo_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);
  foo_cluster.info_->load_report_stats_.upstream_rq_drop_overload_.add(8);
  bar_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);
  bar_cluster.info_->load_report_stats_.upstream_rq_drop_overload_.add(3);

  // Start tracking foo again, we should forget earlier history for foo.
  time_system_.setMonotonicTime(std::chrono::microseconds(43));
  deliverLoadStatsResponse({"foo", "bar"});
  // Stats report foo/bar on timer tick.
  foo_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);
  foo_cluster.info_->load_report_stats_.upstream_rq_drop_overload_.add(9);
  bar_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(1);
  bar_cluster.info_->load_report_stats_.upstream_rq_drop_overload_.add(4);
  time_system_.setMonotonicTime(std::chrono::microseconds(47));
  {
    envoy::config::endpoint::v3::ClusterStats foo_cluster_stats;
    foo_cluster_stats.set_cluster_name("foo");
    foo_cluster_stats.set_cluster_service_name("bar");
    foo_cluster_stats.set_total_dropped_requests(1);
    setDropOverload(foo_cluster_stats, 9);
    foo_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(4));
    envoy::config::endpoint::v3::ClusterStats bar_cluster_stats;
    bar_cluster_stats.set_cluster_name("bar");
    bar_cluster_stats.set_total_dropped_requests(2);
    setDropOverload(bar_cluster_stats, 7);
    bar_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(14));
    expectSendMessage({bar_cluster_stats, foo_cluster_stats});
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();
}

HostSharedPtr makeTestHost(const std::string& hostname,
                           const ::envoy::config::core::v3::Locality& locality) {
  const auto host = std::make_shared<NiceMock<::Envoy::Upstream::MockHost>>();
  ON_CALL(*host, hostname()).WillByDefault(::testing::ReturnRef(hostname));
  ON_CALL(*host, locality()).WillByDefault(::testing::ReturnRef(locality));
  return host;
}

void addStats(const HostSharedPtr& host, double a, double b = 0, double c = 0, double d = 0) {
  host->stats().rq_success_.inc();
  host->loadMetricStats().add("metric_a", a);
  if (b != 0) {
    host->loadMetricStats().add("metric_b", b);
  }
  if (c != 0) {
    host->loadMetricStats().add("metric_c", c);
  }
  if (d != 0) {
    host->loadMetricStats().add("metric_d", d);
  }
}

void addStatExpectation(envoy::config::endpoint::v3::UpstreamLocalityStats* stats,
                        const std::string& metric_name, int num_requests_with_metric,
                        double total_metric_value) {
  auto metric = stats->add_load_metric_stats();
  metric->set_metric_name(metric_name);
  metric->set_num_requests_finished_with_metric(num_requests_with_metric);
  metric->set_total_metric_value(total_metric_value);
}

// Validate that per-locality metrics are aggregated across hosts and included in the load report.
TEST_F(LoadStatsReporterTest, UpstreamLocalityStats) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(3));

  // Set up some load metrics
  NiceMock<MockClusterMockPrioritySet> cluster;
  MockHostSet& host_set_ = *cluster.prioritySet().getMockHostSet(0);

  ::envoy::config::core::v3::Locality locality0, locality1;
  locality0.set_region("mars");
  locality1.set_region("jupiter");
  HostSharedPtr host0 = makeTestHost("host0", locality0), host1 = makeTestHost("host1", locality0),
                host2 = makeTestHost("host2", locality1);
  host_set_.hosts_per_locality_ = makeHostsPerLocality({{host0, host1}, {host2}});

  addStats(host0, 0.11111, 1.0);
  addStats(host0, 0.33333, 0, 3.14159);
  addStats(host1, 0.44444, 0.12345);
  addStats(host2, 10.01, 0, 20.02, 30.03);

  cluster.info_->eds_service_name_ = "bar";
  MockClusterManager::ClusterInfoMaps cluster_info{{{"foo", cluster}}, {}, {}};
  ON_CALL(cm_, clusters()).WillByDefault(Return(cluster_info));
  deliverLoadStatsResponse({"foo"});
  // First stats report on timer tick.
  time_system_.setMonotonicTime(std::chrono::microseconds(4));
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats;
    expected_cluster_stats.set_cluster_name("foo");
    expected_cluster_stats.set_cluster_service_name("bar");
    expected_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(1));

    auto expected_locality0_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality0_stats->mutable_locality()->set_region("mars");
    expected_locality0_stats->set_total_successful_requests(3);
    addStatExpectation(expected_locality0_stats, "metric_a", 3, 0.88888);
    addStatExpectation(expected_locality0_stats, "metric_b", 2, 1.12345);
    addStatExpectation(expected_locality0_stats, "metric_c", 1, 3.14159);

    auto expected_locality1_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality1_stats->mutable_locality()->set_region("jupiter");
    expected_locality1_stats->set_total_successful_requests(1);
    addStatExpectation(expected_locality1_stats, "metric_a", 1, 10.01);
    addStatExpectation(expected_locality1_stats, "metric_c", 1, 20.02);
    addStatExpectation(expected_locality1_stats, "metric_d", 1, 30.03);

    expectSendMessage({expected_cluster_stats});
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();

  // Traffic between previous request and next response. Previous latched metrics are cleared.
  host1->stats().rq_success_.inc();
  host1->loadMetricStats().add("metric_a", 1.41421);
  host1->loadMetricStats().add("metric_e", 2.71828);

  time_system_.setMonotonicTime(std::chrono::microseconds(6));
  deliverLoadStatsResponse({"foo"});
  // Second stats report on timer tick.
  time_system_.setMonotonicTime(std::chrono::microseconds(28));
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats;
    expected_cluster_stats.set_cluster_name("foo");
    expected_cluster_stats.set_cluster_service_name("bar");
    expected_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(24));

    auto expected_locality0_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality0_stats->mutable_locality()->set_region("mars");
    expected_locality0_stats->set_total_successful_requests(1);
    addStatExpectation(expected_locality0_stats, "metric_a", 1, 1.41421);
    addStatExpectation(expected_locality0_stats, "metric_e", 1, 2.71828);

    // No stats for locality 1 since there was no traffic to it.
    expectSendMessage({expected_cluster_stats});
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
