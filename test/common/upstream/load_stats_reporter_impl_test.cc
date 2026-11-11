#include <functional>
#include <memory>

#include "envoy/config/endpoint/v3/load_report.pb.h"
#include "envoy/service/load_stats/v3/lrs.pb.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "source/common/network/address_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/upstream/load_stats_reporter_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/host_set.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;

// The tests in this file provide just coverage over some corner cases in error handling. The test
// for the happy path for LoadStatsReporterImpl is provided in
// //test/integration:load_stats_reporter.
namespace Envoy {
namespace Upstream {
namespace {

// The data plane updates a host's load stats first and then increments the cluster's
// upstream_host_load_updates_ counter (see ClusterLoadReportStats). The mock hosts used in these
// tests are not wired to their cluster, so the tests have to do the same by hand.
void recordHostLoadUpdate(const Cluster& cluster) {
  cluster.info()->loadReportStats().upstream_host_load_updates_.inc();
}

// All tests run with the skipping of idle clusters both enabled and disabled; the reports must be
// identical in both cases.
class LoadStatsReporterImplTest : public testing::TestWithParam<bool> {
public:
  LoadStatsReporterImplTest()
      : retry_timer_(new Event::MockTimer()), response_timer_(new Event::MockTimer()),
        async_client_(new Grpc::MockAsyncClient()) {
    scoped_runtime_.mergeValues({{"envoy.reloadable_features.lrs_skip_idle_clusters",
                                  skipIdleClusters() ? "true" : "false"}});
  }

  bool skipIdleClusters() const { return GetParam(); }

  void TearDown() override {
    if (load_stats_reporter_ != nullptr) {
      // Validate that LoadStatsReporterImpl correctly shuts down by disabling
      // timers and resetting the stream.
      EXPECT_CALL(*retry_timer_, disableTimer());
      EXPECT_CALL(*response_timer_, disableTimer());
      EXPECT_CALL(async_stream_, resetStream());
      load_stats_reporter_.reset();
    }
  }

  void createLoadStatsReporter() {
    InSequence s;
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce([this](Event::TimerCb timer_cb) {
      retry_timer_cb_ = timer_cb;
      return retry_timer_;
    });
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce([this](Event::TimerCb timer_cb) {
      response_timer_cb_ = timer_cb;
      return response_timer_;
    });
    load_stats_reporter_ = std::make_unique<LoadStatsReporterImpl>(
        local_info_, cm_, *stats_store_.rootScope(), Grpc::RawAsyncClientPtr(async_client_),
        dispatcher_);
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

  void deliverLoadStatsResponse(const std::vector<std::string>& cluster_names,
                                bool report_endpoint_granularity = false,
                                bool send_all_clusters = false) {
    std::unique_ptr<envoy::service::load_stats::v3::LoadStatsResponse> response(
        new envoy::service::load_stats::v3::LoadStatsResponse());
    response->mutable_load_reporting_interval()->set_seconds(42);
    response->set_report_endpoint_granularity(report_endpoint_granularity);
    response->set_send_all_clusters(send_all_clusters);
    std::copy(cluster_names.begin(), cluster_names.end(),
              Protobuf::RepeatedPtrFieldBackInserter(response->mutable_clusters()));

    EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
    load_stats_reporter_->onReceiveMessage(std::move(response));
  }

  // Fires the response timer, i.e. sends a load report and starts the next load report period.
  void sendLoadReport() {
    EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
    response_timer_cb_();
  }

  // Makes the mock cluster manager expose the given clusters, keyed by their info()->name(), to
  // forEachActiveCluster().
  void setActiveClusters(const std::vector<std::reference_wrapper<const Cluster>>& clusters) {
    ClusterManager::ClusterInfoMaps cluster_maps;
    for (const Cluster& cluster : clusters) {
      cluster_maps.active_clusters_.emplace(cluster.info()->name(), cluster);
    }
    ON_CALL(cm_, clusters()).WillByDefault(Return(cluster_maps));
  }

  void
  addEndpointStatExpectation(envoy::config::endpoint::v3::UpstreamEndpointStats* endpoint_stats,
                             const std::string& metric_name, uint64_t rq_count,
                             double total_value) {
    auto* metric = endpoint_stats->add_load_metric_stats();
    metric->set_metric_name(metric_name);
    metric->set_num_requests_finished_with_metric(rq_count);
    metric->set_total_metric_value(total_value);
  }

  void setDropOverload(envoy::config::endpoint::v3::ClusterStats& cluster_stats, uint64_t count) {
    auto* dropped_request = cluster_stats.add_dropped_requests();
    dropped_request->set_category("drop_overload");
    dropped_request->set_dropped_count(count);
  }

  envoy::config::endpoint::v3::ClusterStats makeClusterStats(const std::string& cluster_name,
                                                             uint64_t interval_us) {
    envoy::config::endpoint::v3::ClusterStats cluster_stats;
    cluster_stats.set_cluster_name(cluster_name);
    cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(interval_us));
    return cluster_stats;
  }

  // Expects that the hosts of host_set are (not) visited by the next load report, depending on
  // whether idle clusters are skipped. The expectation is verified and cleared by
  // verifyHostVisitExpectations().
  void expectHostsVisited(MockHostSet& host_set, bool visited_when_skipping_idle_clusters) {
    if (visited_when_skipping_idle_clusters || !skipIdleClusters()) {
      EXPECT_CALL(host_set, hostsPerLocality()).Times(AtLeast(1));
    } else {
      EXPECT_CALL(host_set, hostsPerLocality()).Times(0);
    }
    host_sets_with_expectations_.push_back(&host_set);
  }

  void verifyHostVisitExpectations() {
    for (MockHostSet* host_set : host_sets_with_expectations_) {
      testing::Mock::VerifyAndClearExpectations(host_set);
    }
    host_sets_with_expectations_.clear();
  }

  TestScopedRuntime scoped_runtime_;
  Event::SimulatedTimeSystem time_system_;
  NiceMock<Upstream::MockClusterManager> cm_;
  Event::MockDispatcher dispatcher_;
  Stats::IsolatedStoreImpl stats_store_;
  Event::MockTimer* retry_timer_;
  Event::TimerCb retry_timer_cb_;
  Event::MockTimer* response_timer_;
  Event::TimerCb response_timer_cb_;
  Grpc::MockAsyncStream async_stream_;
  Grpc::MockAsyncClient* async_client_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  std::unique_ptr<LoadStatsReporterImpl> load_stats_reporter_;
  std::vector<MockHostSet*> host_sets_with_expectations_;
};

// Validate that stream creation results in a timer based retry.
TEST_P(LoadStatsReporterImplTest, StreamCreationFailure) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(nullptr));
  EXPECT_CALL(*retry_timer_, enableTimer(_, _));
  createLoadStatsReporter();
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  retry_timer_cb_();
}

TEST_P(LoadStatsReporterImplTest, TestPubSub) {
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

// Validate treatment of existing clusters across updates. The stats of a cluster are latched when
// it starts being tracked, so that load incurred before that is not reported.
TEST_P(LoadStatsReporterImplTest, ExistingClusters) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  // Initially, we have no clusters to report on.
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(3));
  // Start reporting on foo. The drops that happened before are latched away.
  NiceMock<MockClusterMockPrioritySet> foo_cluster;
  foo_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(2);
  foo_cluster.info_->eds_service_name_ = "bar";
  NiceMock<MockClusterMockPrioritySet> bar_cluster;
  ON_CALL(cm_, getActiveCluster("foo"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(foo_cluster)));
  ON_CALL(cm_, getActiveCluster("bar"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(bar_cluster)));
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

  // Some traffic on foo/bar in between previous request and next response. bar is not tracked
  // yet, so its drops are latched away when it starts being tracked below.
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

  // Use a concrete Ipv4Instance instead of a mock address
  auto address = std::make_shared<Envoy::Network::Address::Ipv4Instance>("127.0.0.1", 80);
  ON_CALL(*host, address()).WillByDefault(::testing::Return(address));
  return host;
}

// Records a successful request with custom load metrics on host, which belongs to cluster.
void addStats(const Cluster& cluster, const HostSharedPtr& host, double a, double b = 0,
              double c = 0, double d = 0) {
  host->stats().rq_total_.inc();
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
  recordHostLoadUpdate(cluster);
}

void addStatExpectation(envoy::config::endpoint::v3::UpstreamLocalityStats* stats,
                        const std::string& metric_name, int num_requests_with_metric,
                        double total_metric_value) {
  auto metric = stats->add_load_metric_stats();
  metric->set_metric_name(metric_name);
  metric->set_num_requests_finished_with_metric(num_requests_with_metric);
  metric->set_total_metric_value(total_metric_value);
}

// This test validates that the LoadStatsReporterImpl correctly handles and reports
// endpoint-level granularity load metrics when the feature is enabled. It sets
// up a cluster with a host, simulates load metrics, and ensures that the
// generated load report includes the expected endpoint-level statistics.
TEST_P(LoadStatsReporterImplTest, EndpointLevelLoadStatsReporting) {
  // Enable endpoint granularity
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(100));

  NiceMock<MockClusterMockPrioritySet> cluster;
  MockHostSet& host_set = *cluster.prioritySet().getMockHostSet(0);
  ::envoy::config::core::v3::Locality locality;
  locality.set_region("test_region");

  // Create two hosts with different metric values
  HostSharedPtr host1 = makeTestHost("host1", locality);
  HostSharedPtr host2 = makeTestHost("host2", locality);
  host_set.hosts_per_locality_ = makeHostsPerLocality({{host1, host2}});

  cluster.info_->eds_service_name_ = "eds_service_for_foo";

  ON_CALL(cm_, getActiveCluster("foo"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(cluster)));
  deliverLoadStatsResponse({"foo"}, true);
  addStats(cluster, host1, 10.0); // metric_a = 10.0
  addStats(cluster, host2, 20.0); // metric_a = 20.0
  time_system_.setMonotonicTime(std::chrono::microseconds(101));
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats;

    expected_cluster_stats.set_cluster_name("foo");
    expected_cluster_stats.set_cluster_service_name("eds_service_for_foo");
    expected_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(1));

    auto* expected_locality_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality_stats->mutable_locality()->MergeFrom(locality);
    expected_locality_stats->set_priority(0);
    expected_locality_stats->set_total_successful_requests(2);
    expected_locality_stats->set_total_issued_requests(2);
    // Locality metric is the sum
    addStatExpectation(expected_locality_stats, "metric_a", 2, 30.0);

    // Endpoint 1
    auto* endpoint_stats1 = expected_locality_stats->add_upstream_endpoint_stats();
    endpoint_stats1->mutable_address()->mutable_socket_address()->set_address("127.0.0.1");
    endpoint_stats1->mutable_address()->mutable_socket_address()->set_port_value(80);
    endpoint_stats1->set_total_successful_requests(1);
    endpoint_stats1->set_total_issued_requests(1);
    addEndpointStatExpectation(endpoint_stats1, "metric_a", 1, 10.0);

    // Endpoint 2
    auto* endpoint_stats2 = expected_locality_stats->add_upstream_endpoint_stats();
    endpoint_stats2->mutable_address()->mutable_socket_address()->set_address("127.0.0.1");
    endpoint_stats2->mutable_address()->mutable_socket_address()->set_port_value(80);
    endpoint_stats2->set_total_successful_requests(1);
    endpoint_stats2->set_total_issued_requests(1);
    addEndpointStatExpectation(endpoint_stats2, "metric_a", 1, 20.0);

    std::vector<envoy::config::endpoint::v3::ClusterStats> expected_cluster_stats_vector = {
        expected_cluster_stats};

    expectSendMessage(expected_cluster_stats_vector);
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();
}

// This test validates that endpoint stats are not reported if the endpoint has no load stat
// updates.
TEST_P(LoadStatsReporterImplTest, EndpointLevelLoadStatsReportingNoUpdate) {
  // Enable endpoint granularity
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(100));

  NiceMock<MockClusterMockPrioritySet> cluster;
  MockHostSet& host_set = *cluster.prioritySet().getMockHostSet(0);
  ::envoy::config::core::v3::Locality locality;
  locality.set_region("test_region");

  // Create two hosts, but only one will have stats.
  HostSharedPtr host1 = makeTestHost("host1", locality);
  HostSharedPtr host2 = makeTestHost("host2", locality);
  host_set.hosts_per_locality_ = makeHostsPerLocality({{host1, host2}});

  cluster.info_->eds_service_name_ = "eds_service_for_foo";

  ON_CALL(cm_, getActiveCluster("foo"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(cluster)));
  deliverLoadStatsResponse({"foo"}, true);
  addStats(cluster, host1, 10.0);
  // Host2 has no updates. Its stats are all 0 and will be latched as such.
  time_system_.setMonotonicTime(std::chrono::microseconds(101));
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats;

    expected_cluster_stats.set_cluster_name("foo");
    expected_cluster_stats.set_cluster_service_name("eds_service_for_foo");
    expected_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(1));

    auto* expected_locality_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality_stats->mutable_locality()->MergeFrom(locality);
    expected_locality_stats->set_priority(0);
    // Locality stats should only reflect host1.
    expected_locality_stats->set_total_successful_requests(1);
    expected_locality_stats->set_total_issued_requests(1);
    addStatExpectation(expected_locality_stats, "metric_a", 1, 10.0);

    // Only Endpoint 1 should be in the report.
    auto* endpoint_stats1 = expected_locality_stats->add_upstream_endpoint_stats();
    endpoint_stats1->mutable_address()->mutable_socket_address()->set_address("127.0.0.1");
    endpoint_stats1->mutable_address()->mutable_socket_address()->set_port_value(80);
    endpoint_stats1->set_total_successful_requests(1);
    endpoint_stats1->set_total_issued_requests(1);
    addEndpointStatExpectation(endpoint_stats1, "metric_a", 1, 10.0);

    std::vector<envoy::config::endpoint::v3::ClusterStats> expected_cluster_stats_vector = {
        expected_cluster_stats};

    expectSendMessage(expected_cluster_stats_vector);
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();
}

// Validate that per-locality metrics are aggregated across hosts and included in the load report.
TEST_P(LoadStatsReporterImplTest, UpstreamLocalityStats) {
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

  cluster.info_->eds_service_name_ = "bar";
  ON_CALL(cm_, getActiveCluster("foo"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(cluster)));
  deliverLoadStatsResponse({"foo"});

  addStats(cluster, host0, 0.11111, 1.0);
  addStats(cluster, host0, 0.33333, 0, 3.14159);
  addStats(cluster, host1, 0.44444, 0.12345);
  addStats(cluster, host2, 10.01, 0, 20.02, 30.03);

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
    expected_locality0_stats->set_total_issued_requests(3);
    addStatExpectation(expected_locality0_stats, "metric_a", 3, 0.88888);
    addStatExpectation(expected_locality0_stats, "metric_b", 2, 1.12345);
    addStatExpectation(expected_locality0_stats, "metric_c", 1, 3.14159);

    auto expected_locality1_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality1_stats->mutable_locality()->set_region("jupiter");
    expected_locality1_stats->set_total_successful_requests(1);
    expected_locality1_stats->set_total_issued_requests(1);
    addStatExpectation(expected_locality1_stats, "metric_a", 1, 10.01);
    addStatExpectation(expected_locality1_stats, "metric_c", 1, 20.02);
    addStatExpectation(expected_locality1_stats, "metric_d", 1, 30.03);

    expectSendMessage({expected_cluster_stats});
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();

  // Traffic between previous request and next response. Previous latched metrics are cleared.
  host1->stats().rq_success_.inc();

  host1->stats().rq_total_.inc();
  host1->loadMetricStats().add("metric_a", 1.41421);
  host1->loadMetricStats().add("metric_e", 2.71828);
  recordHostLoadUpdate(cluster);

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
    expected_locality0_stats->set_total_issued_requests(1);
    addStatExpectation(expected_locality0_stats, "metric_a", 1, 1.41421);
    addStatExpectation(expected_locality0_stats, "metric_e", 1, 2.71828);

    // No stats for locality 1 since there was no traffic to it.
    expectSendMessage({expected_cluster_stats});
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();
}

// Validate that the client can recover from a remote stream closure via retry.
TEST_P(LoadStatsReporterImplTest, RemoteStreamClose) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  EXPECT_CALL(*response_timer_, disableTimer());
  EXPECT_CALL(*retry_timer_, enableTimer(_, _));
  load_stats_reporter_->onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Canceled, "");
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  retry_timer_cb_();
  EXPECT_EQ(load_stats_reporter_->getStats().errors_.value(), 1);
  EXPECT_EQ(load_stats_reporter_->getStats().retries_.value(), 1);
}

// Validate that errors stat is not incremented for a graceful stream termination.
TEST_P(LoadStatsReporterImplTest, RemoteStreamGracefulClose) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  EXPECT_CALL(*response_timer_, disableTimer());
  EXPECT_CALL(*retry_timer_, enableTimer(_, _));
  load_stats_reporter_->onRemoteClose(Grpc::Status::WellKnownGrpcStatus::Ok, "");
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  retry_timer_cb_();
  EXPECT_EQ(load_stats_reporter_->getStats().errors_.value(), 0);
  EXPECT_EQ(load_stats_reporter_->getStats().retries_.value(), 1);
}

// Validate that when rq_active is non-zero, a load report is sent even if rq_issued is 0.
TEST_P(LoadStatsReporterImplTest, ReportLoadWhenRqActiveIsNonZero) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.report_load_for_non_zero_stats", "false"}});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(100));

  NiceMock<MockClusterMockPrioritySet> cluster;
  MockHostSet& host_set = *cluster.prioritySet().getMockHostSet(0);
  ::envoy::config::core::v3::Locality locality;
  locality.set_region("test_region");

  HostSharedPtr host1 = makeTestHost("host1", locality);
  host_set.hosts_per_locality_ = makeHostsPerLocality({{host1}});

  cluster.info_->eds_service_name_ = "eds_service_for_foo";

  ON_CALL(cm_, getActiveCluster("foo"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(cluster)));
  deliverLoadStatsResponse({"foo"});
  // Set rq_active to non-zero, rq_issued to zero.
  host1->stats().rq_active_.set(5);
  // Do not call addStats to ensure rq_issued and rq_success remain 0.
  time_system_.setMonotonicTime(std::chrono::microseconds(101));
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats;

    expected_cluster_stats.set_cluster_name("foo");
    expected_cluster_stats.set_cluster_service_name("eds_service_for_foo");
    expected_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(1));

    auto* expected_locality_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality_stats->mutable_locality()->MergeFrom(locality);
    expected_locality_stats->set_priority(0);
    expected_locality_stats->set_total_successful_requests(0);
    // Load report send when there is 0 QPS in this poll cycle.
    expected_locality_stats->set_total_issued_requests(0);
    expected_locality_stats->set_total_requests_in_progress(5);

    std::vector<envoy::config::endpoint::v3::ClusterStats> expected_cluster_stats_vector = {
        expected_cluster_stats};

    expectSendMessage(expected_cluster_stats_vector);
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();
}

// Validate that when envoy.reloadable_features.report_load_for_non_zero_stats is true, a load
// report is sent if only rq_success is non-zero.
TEST_P(LoadStatsReporterImplTest, ReportLoadForNonZeroStatsRqSuccess) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.report_load_for_non_zero_stats", "true"}});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(100));

  NiceMock<MockClusterMockPrioritySet> cluster;
  MockHostSet& host_set = *cluster.prioritySet().getMockHostSet(0);
  ::envoy::config::core::v3::Locality locality;
  locality.set_region("test_region");

  HostSharedPtr host1 = makeTestHost("host1", locality);
  host_set.hosts_per_locality_ = makeHostsPerLocality({{host1}});

  cluster.info_->eds_service_name_ = "eds_service_for_foo";

  ON_CALL(cm_, getActiveCluster("foo"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(cluster)));
  deliverLoadStatsResponse({"foo"});
  // Set rq_success to non-zero, all others to zero.
  host1->stats().rq_success_.inc();
  recordHostLoadUpdate(cluster);
  time_system_.setMonotonicTime(std::chrono::microseconds(101));
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats;

    expected_cluster_stats.set_cluster_name("foo");
    expected_cluster_stats.set_cluster_service_name("eds_service_for_foo");
    expected_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(1));

    auto* expected_locality_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality_stats->mutable_locality()->MergeFrom(locality);
    expected_locality_stats->set_priority(0);
    expected_locality_stats->set_total_successful_requests(1);
    expected_locality_stats->set_total_error_requests(0);
    expected_locality_stats->set_total_requests_in_progress(0);
    expected_locality_stats->set_total_issued_requests(0);

    std::vector<envoy::config::endpoint::v3::ClusterStats> expected_cluster_stats_vector = {
        expected_cluster_stats};

    expectSendMessage(expected_cluster_stats_vector);
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();
}

// Validate that when envoy.reloadable_features.report_load_for_non_zero_stats is true, a load
// report is sent if only rq_error is non-zero.
TEST_P(LoadStatsReporterImplTest, ReportLoadForNonZeroStatsRqError) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.report_load_for_non_zero_stats", "true"}});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(100));

  NiceMock<MockClusterMockPrioritySet> cluster;
  MockHostSet& host_set = *cluster.prioritySet().getMockHostSet(0);
  ::envoy::config::core::v3::Locality locality;
  locality.set_region("test_region");

  HostSharedPtr host1 = makeTestHost("host1", locality);
  host_set.hosts_per_locality_ = makeHostsPerLocality({{host1}});

  cluster.info_->eds_service_name_ = "eds_service_for_foo";

  ON_CALL(cm_, getActiveCluster("foo"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(cluster)));
  deliverLoadStatsResponse({"foo"});
  // Set rq_error to non-zero, all others to zero.
  host1->stats().rq_error_.inc();
  recordHostLoadUpdate(cluster);
  time_system_.setMonotonicTime(std::chrono::microseconds(101));
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats;

    expected_cluster_stats.set_cluster_name("foo");
    expected_cluster_stats.set_cluster_service_name("eds_service_for_foo");
    expected_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(1));

    auto* expected_locality_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality_stats->mutable_locality()->MergeFrom(locality);
    expected_locality_stats->set_priority(0);
    expected_locality_stats->set_total_successful_requests(0);
    expected_locality_stats->set_total_error_requests(1);
    expected_locality_stats->set_total_requests_in_progress(0);
    expected_locality_stats->set_total_issued_requests(0);

    std::vector<envoy::config::endpoint::v3::ClusterStats> expected_cluster_stats_vector = {
        expected_cluster_stats};

    expectSendMessage(expected_cluster_stats_vector);
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();
}

// Validate that when envoy.reloadable_features.report_load_for_non_zero_stats is true, a load
// report is sent if only custom metrics are present.
TEST_P(LoadStatsReporterImplTest, ReportLoadForNonZeroStatsCustomMetric) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.report_load_for_non_zero_stats", "true"}});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(100));

  NiceMock<MockClusterMockPrioritySet> cluster;
  MockHostSet& host_set = *cluster.prioritySet().getMockHostSet(0);
  ::envoy::config::core::v3::Locality locality;
  locality.set_region("test_region");

  HostSharedPtr host1 = makeTestHost("host1", locality);
  host_set.hosts_per_locality_ = makeHostsPerLocality({{host1}});

  cluster.info_->eds_service_name_ = "eds_service_for_foo";

  ON_CALL(cm_, getActiveCluster("foo"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(cluster)));
  deliverLoadStatsResponse({"foo"});
  // Add a custom metric, all other counters zero.
  host1->loadMetricStats().add("metric_a", 1.0);
  recordHostLoadUpdate(cluster);
  time_system_.setMonotonicTime(std::chrono::microseconds(101));
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats;

    expected_cluster_stats.set_cluster_name("foo");
    expected_cluster_stats.set_cluster_service_name("eds_service_for_foo");
    expected_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(1));

    auto* expected_locality_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality_stats->mutable_locality()->MergeFrom(locality);
    expected_locality_stats->set_priority(0);
    expected_locality_stats->set_total_successful_requests(0);
    expected_locality_stats->set_total_error_requests(0);
    expected_locality_stats->set_total_requests_in_progress(0);
    expected_locality_stats->set_total_issued_requests(0);
    addStatExpectation(expected_locality_stats, "metric_a", 1, 1.0);

    std::vector<envoy::config::endpoint::v3::ClusterStats> expected_cluster_stats_vector = {
        expected_cluster_stats};

    expectSendMessage(expected_cluster_stats_vector);
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();
}

// Validate that when envoy.reloadable_features.report_load_for_non_zero_stats is false, a load
// report is NOT sent if only rq_success is non-zero.
TEST_P(LoadStatsReporterImplTest, ReportLoadForNonZeroStatsDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.report_load_for_non_zero_stats", "false"}});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(100));

  NiceMock<MockClusterMockPrioritySet> cluster;
  MockHostSet& host_set = *cluster.prioritySet().getMockHostSet(0);
  ::envoy::config::core::v3::Locality locality;
  locality.set_region("test_region");

  HostSharedPtr host1 = makeTestHost("host1", locality);
  host_set.hosts_per_locality_ = makeHostsPerLocality({{host1}});

  cluster.info_->eds_service_name_ = "eds_service_for_foo";

  ON_CALL(cm_, getActiveCluster("foo"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(cluster)));
  deliverLoadStatsResponse({"foo"});
  // Set rq_success to non-zero, all others to zero.
  host1->stats().rq_success_.inc();
  recordHostLoadUpdate(cluster);
  time_system_.setMonotonicTime(std::chrono::microseconds(101));
  {
    // Expect no UpstreamLocalityStats
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats;
    expected_cluster_stats.set_cluster_name("foo");
    expected_cluster_stats.set_cluster_service_name("eds_service_for_foo");
    expected_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(1));

    std::vector<envoy::config::endpoint::v3::ClusterStats> expected_cluster_stats_vector = {
        expected_cluster_stats};

    expectSendMessage(expected_cluster_stats_vector);
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();
}

// Regression test: the load stats reporter must skip empty locality groups instead of
// crashing with a null dereference on hosts[0].
TEST_P(LoadStatsReporterImplTest, EmptyLocalityGroupDoesNotCrash) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(0));

  NiceMock<MockClusterMockPrioritySet> cluster;
  MockHostSet& host_set = *cluster.prioritySet().getMockHostSet(0);

  ::envoy::config::core::v3::Locality locality0, locality1;
  locality0.set_region("mars");
  locality1.set_region("jupiter");
  HostSharedPtr host0 = makeTestHost("host0", locality0);
  HostSharedPtr host1 = makeTestHost("host1", locality1);

  host_set.hosts_per_locality_ = makeHostsPerLocality({{host0}, {host1}});

  ON_CALL(cm_, getActiveCluster("foo"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(cluster)));
  deliverLoadStatsResponse({"foo"});
  addStats(cluster, host0, 1.0);
  addStats(cluster, host1, 2.0);
  time_system_.setMonotonicTime(std::chrono::microseconds(1));
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats;
    expected_cluster_stats.set_cluster_name("foo");
    expected_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(1));

    auto* expected_locality0_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality0_stats->mutable_locality()->set_region("mars");
    expected_locality0_stats->set_total_successful_requests(1);
    expected_locality0_stats->set_total_issued_requests(1);
    addStatExpectation(expected_locality0_stats, "metric_a", 1, 1.0);

    auto* expected_locality1_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality1_stats->mutable_locality()->set_region("jupiter");
    expected_locality1_stats->set_total_successful_requests(1);
    expected_locality1_stats->set_total_issued_requests(1);
    addStatExpectation(expected_locality1_stats, "metric_a", 1, 2.0);

    expectSendMessage({expected_cluster_stats});
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();

  // Simulate state produced when the last host is removed from a locality group.
  host_set.hosts_per_locality_ = makeHostsPerLocality({{host0}, {}});

  addStats(cluster, host0, 3.0);

  deliverLoadStatsResponse({"foo"});
  time_system_.setMonotonicTime(std::chrono::microseconds(2));
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats;
    expected_cluster_stats.set_cluster_name("foo");
    expected_cluster_stats.mutable_load_report_interval()->MergeFrom(
        Protobuf::util::TimeUtil::MicrosecondsToDuration(1));

    auto* expected_locality_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality_stats->mutable_locality()->set_region("mars");
    expected_locality_stats->set_total_successful_requests(1);
    expected_locality_stats->set_total_issued_requests(1);
    addStatExpectation(expected_locality_stats, "metric_a", 1, 3.0);

    expectSendMessage({expected_cluster_stats});
  }
  EXPECT_CALL(*response_timer_, enableTimer(std::chrono::milliseconds(42000), _));
  response_timer_cb_();
}

// Validate that the hosts of a cluster are only visited when the cluster had host load updates
// since the last report (if idle clusters are skipped), and that the reports are the same either
// way.
TEST_P(LoadStatsReporterImplTest, IdleClusterHostsAreNotVisited) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(0));

  NiceMock<MockClusterMockPrioritySet> cluster;
  MockHostSet& host_set = *cluster.prioritySet().getMockHostSet(0);
  ::envoy::config::core::v3::Locality locality;
  locality.set_region("mars");
  HostSharedPtr host = makeTestHost("host", locality);
  host_set.hosts_per_locality_ = makeHostsPerLocality({{host}});
  ON_CALL(cm_, getActiveCluster("foo"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(cluster)));
  deliverLoadStatsResponse({"foo"});

  // A newly tracked cluster is always visited once, even without any updates.
  time_system_.setMonotonicTime(std::chrono::microseconds(1));
  expectHostsVisited(host_set, true);
  expectSendMessage({makeClusterStats("foo", 1)});
  sendLoadReport();
  verifyHostVisitExpectations();

  // Without any host load updates the hosts are not visited again. Dropped requests are still
  // reported, they are tracked at the cluster level.
  cluster.info_->load_report_stats_.upstream_rq_dropped_.add(3);
  time_system_.setMonotonicTime(std::chrono::microseconds(2));
  expectHostsVisited(host_set, false);
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats = makeClusterStats("foo", 1);
    expected_cluster_stats.set_total_dropped_requests(3);
    expectSendMessage({expected_cluster_stats});
  }
  sendLoadReport();
  verifyHostVisitExpectations();

  // A host load update makes the hosts be visited again.
  addStats(cluster, host, 1.0);
  time_system_.setMonotonicTime(std::chrono::microseconds(3));
  expectHostsVisited(host_set, true);
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats = makeClusterStats("foo", 1);
    auto* expected_locality_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality_stats->mutable_locality()->set_region("mars");
    expected_locality_stats->set_total_successful_requests(1);
    expected_locality_stats->set_total_issued_requests(1);
    addStatExpectation(expected_locality_stats, "metric_a", 1, 1.0);
    expectSendMessage({expected_cluster_stats});
  }
  sendLoadReport();
  verifyHostVisitExpectations();

  // And the cluster is idle again afterwards.
  time_system_.setMonotonicTime(std::chrono::microseconds(4));
  expectHostsVisited(host_set, false);
  expectSendMessage({makeClusterStats("foo", 1)});
  sendLoadReport();
  verifyHostVisitExpectations();
}

// Validate that a request that completes in a later period than it was issued in is reported
// in the period it completed, regardless of whether idle clusters are skipped.
TEST_P(LoadStatsReporterImplTest, RequestCompletionIsReportedWhenIdleClustersAreSkipped) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.report_load_for_non_zero_stats", "true"}});

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(0));

  NiceMock<MockClusterMockPrioritySet> cluster;
  MockHostSet& host_set = *cluster.prioritySet().getMockHostSet(0);
  ::envoy::config::core::v3::Locality locality;
  locality.set_region("mars");
  HostSharedPtr host = makeTestHost("host", locality);
  host_set.hosts_per_locality_ = makeHostsPerLocality({{host}});
  ON_CALL(cm_, getActiveCluster("foo"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(cluster)));
  deliverLoadStatsResponse({"foo"});

  // Period 1: a request is issued and stays in progress.
  host->stats().rq_total_.inc();
  host->stats().rq_active_.inc();
  recordHostLoadUpdate(cluster);
  time_system_.setMonotonicTime(std::chrono::microseconds(1));
  expectHostsVisited(host_set, true);
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats = makeClusterStats("foo", 1);
    auto* expected_locality_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality_stats->mutable_locality()->set_region("mars");
    expected_locality_stats->set_total_issued_requests(1);
    expected_locality_stats->set_total_requests_in_progress(1);
    expectSendMessage({expected_cluster_stats});
  }
  sendLoadReport();
  verifyHostVisitExpectations();

  // Period 2: the request completes successfully, nothing new is issued.
  host->stats().rq_active_.dec();
  host->stats().rq_success_.inc();
  recordHostLoadUpdate(cluster);
  time_system_.setMonotonicTime(std::chrono::microseconds(2));
  expectHostsVisited(host_set, true);
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats = makeClusterStats("foo", 1);
    auto* expected_locality_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality_stats->mutable_locality()->set_region("mars");
    expected_locality_stats->set_total_successful_requests(1);
    expectSendMessage({expected_cluster_stats});
  }
  sendLoadReport();
  verifyHostVisitExpectations();

  // Period 3: a custom load metric is recorded after the fact (e.g. by an access logger).
  host->loadMetricStats().add("metric_a", 7.0);
  recordHostLoadUpdate(cluster);
  time_system_.setMonotonicTime(std::chrono::microseconds(3));
  expectHostsVisited(host_set, true);
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats = makeClusterStats("foo", 1);
    auto* expected_locality_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality_stats->mutable_locality()->set_region("mars");
    addStatExpectation(expected_locality_stats, "metric_a", 1, 7.0);
    expectSendMessage({expected_cluster_stats});
  }
  sendLoadReport();
  verifyHostVisitExpectations();

  // Period 4: idle.
  time_system_.setMonotonicTime(std::chrono::microseconds(4));
  expectHostsVisited(host_set, false);
  expectSendMessage({makeClusterStats("foo", 1)});
  sendLoadReport();
  verifyHostVisitExpectations();
}

// Validate that a cluster with requests in progress keeps being visited and reported until the
// requests are gone, even if they end without being counted as success or error.
TEST_P(LoadStatsReporterImplTest, RequestsInProgressKeepClusterVisited) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(0));

  NiceMock<MockClusterMockPrioritySet> cluster;
  MockHostSet& host_set = *cluster.prioritySet().getMockHostSet(0);
  ::envoy::config::core::v3::Locality locality;
  locality.set_region("mars");
  HostSharedPtr host = makeTestHost("host", locality);
  host_set.hosts_per_locality_ = makeHostsPerLocality({{host}});
  ON_CALL(cm_, getActiveCluster("foo"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(cluster)));
  deliverLoadStatsResponse({"foo"});

  // Period 1: a long-lived request is issued.
  host->stats().rq_total_.inc();
  host->stats().rq_active_.inc();
  recordHostLoadUpdate(cluster);
  time_system_.setMonotonicTime(std::chrono::microseconds(1));
  expectHostsVisited(host_set, true);
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats = makeClusterStats("foo", 1);
    auto* expected_locality_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality_stats->mutable_locality()->set_region("mars");
    expected_locality_stats->set_total_issued_requests(1);
    expected_locality_stats->set_total_requests_in_progress(1);
    expectSendMessage({expected_cluster_stats});
  }
  sendLoadReport();
  verifyHostVisitExpectations();

  // Period 2: no updates, but the request is still in progress and must be reported as such.
  time_system_.setMonotonicTime(std::chrono::microseconds(2));
  expectHostsVisited(host_set, true);
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats = makeClusterStats("foo", 1);
    auto* expected_locality_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality_stats->mutable_locality()->set_region("mars");
    expected_locality_stats->set_total_requests_in_progress(1);
    expectSendMessage({expected_cluster_stats});
  }
  sendLoadReport();
  verifyHostVisitExpectations();

  // Period 3: the request ends without being counted as a success or error (e.g. a reset), so
  // there is no host load update. The cluster is visited once more and has nothing to report.
  host->stats().rq_active_.dec();
  time_system_.setMonotonicTime(std::chrono::microseconds(3));
  expectHostsVisited(host_set, true);
  expectSendMessage({makeClusterStats("foo", 1)});
  sendLoadReport();
  verifyHostVisitExpectations();

  // Period 4: idle.
  time_system_.setMonotonicTime(std::chrono::microseconds(4));
  expectHostsVisited(host_set, false);
  expectSendMessage({makeClusterStats("foo", 1)});
  sendLoadReport();
  verifyHostVisitExpectations();
}

// Validate that the stats of a cluster are latched when it starts being tracked, so that load
// incurred before that is not reported, and that load incurred afterwards is.
TEST_P(LoadStatsReporterImplTest, NewlyTrackedClusterStatsAreLatched) {
  // Custom load metrics are only reported together with request stats when this is false.
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.report_load_for_non_zero_stats", "false"}});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(0));

  NiceMock<MockClusterMockPrioritySet> cluster;
  MockHostSet& host_set = *cluster.prioritySet().getMockHostSet(0);
  ::envoy::config::core::v3::Locality locality;
  locality.set_region("mars");
  HostSharedPtr host = makeTestHost("host", locality);
  host_set.hosts_ = {host};
  host_set.hosts_per_locality_ = makeHostsPerLocality({{host}});
  ON_CALL(cm_, getActiveCluster("foo"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(cluster)));

  // Load before the cluster is tracked.
  addStats(cluster, host, 1.0);
  cluster.info_->load_report_stats_.upstream_rq_dropped_.add(2);
  deliverLoadStatsResponse({"foo"});

  // Only custom load metrics are not latched when a cluster starts being tracked; they are
  // reported with the first report that includes the locality.
  time_system_.setMonotonicTime(std::chrono::microseconds(1));
  expectSendMessage({makeClusterStats("foo", 1)});
  sendLoadReport();

  // Load after the cluster is tracked.
  addStats(cluster, host, 3.0);
  cluster.info_->load_report_stats_.upstream_rq_dropped_.add(4);
  time_system_.setMonotonicTime(std::chrono::microseconds(2));
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats = makeClusterStats("foo", 1);
    expected_cluster_stats.set_total_dropped_requests(4);
    auto* expected_locality_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality_stats->mutable_locality()->set_region("mars");
    expected_locality_stats->set_total_successful_requests(1);
    expected_locality_stats->set_total_issued_requests(1);
    addStatExpectation(expected_locality_stats, "metric_a", 2, 4.0);
    expectSendMessage({expected_cluster_stats});
  }
  sendLoadReport();
}

// Same as NewlyTrackedClusterStatsAreLatched with
// envoy.reloadable_features.report_load_for_non_zero_stats enabled: the custom load metrics
// recorded before the cluster was tracked are then reported on their own by the first report.
TEST_P(LoadStatsReporterImplTest, NewlyTrackedClusterStatsAreLatchedReportLoadForNonZeroStats) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.report_load_for_non_zero_stats", "true"}});
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(0));

  NiceMock<MockClusterMockPrioritySet> cluster;
  MockHostSet& host_set = *cluster.prioritySet().getMockHostSet(0);
  ::envoy::config::core::v3::Locality locality;
  locality.set_region("mars");
  HostSharedPtr host = makeTestHost("host", locality);
  host_set.hosts_ = {host};
  host_set.hosts_per_locality_ = makeHostsPerLocality({{host}});
  ON_CALL(cm_, getActiveCluster("foo"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(cluster)));

  // Load before the cluster is tracked.
  addStats(cluster, host, 1.0);
  cluster.info_->load_report_stats_.upstream_rq_dropped_.add(2);
  deliverLoadStatsResponse({"foo"});

  // The request stats and dropped requests are latched, the custom load metrics are not.
  time_system_.setMonotonicTime(std::chrono::microseconds(1));
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats = makeClusterStats("foo", 1);
    auto* expected_locality_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality_stats->mutable_locality()->set_region("mars");
    addStatExpectation(expected_locality_stats, "metric_a", 1, 1.0);
    expectSendMessage({expected_cluster_stats});
  }
  sendLoadReport();

  // Load after the cluster is tracked.
  addStats(cluster, host, 3.0);
  cluster.info_->load_report_stats_.upstream_rq_dropped_.add(4);
  time_system_.setMonotonicTime(std::chrono::microseconds(2));
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats = makeClusterStats("foo", 1);
    expected_cluster_stats.set_total_dropped_requests(4);
    auto* expected_locality_stats = expected_cluster_stats.add_upstream_locality_stats();
    expected_locality_stats->mutable_locality()->set_region("mars");
    expected_locality_stats->set_total_successful_requests(1);
    expected_locality_stats->set_total_issued_requests(1);
    addStatExpectation(expected_locality_stats, "metric_a", 1, 3.0);
    expectSendMessage({expected_cluster_stats});
  }
  sendLoadReport();
}

// Validate that a cluster selected by the LRS server that does not exist yet is reported once it
// exists, including the load incurred since it was selected.
TEST_P(LoadStatsReporterImplTest, SelectedClusterAppearsLater) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(0));
  deliverLoadStatsResponse({"foo"});

  // foo does not exist: nothing is reported.
  time_system_.setMonotonicTime(std::chrono::microseconds(1));
  expectSendMessage({});
  sendLoadReport();

  // foo appears, with load that is not latched away because its measurement interval already
  // started.
  NiceMock<MockClusterMockPrioritySet> cluster;
  cluster.info_->load_report_stats_.upstream_rq_dropped_.add(2);
  ON_CALL(cm_, getActiveCluster("foo"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(cluster)));
  time_system_.setMonotonicTime(std::chrono::microseconds(5));
  {
    envoy::config::endpoint::v3::ClusterStats expected_cluster_stats = makeClusterStats("foo", 5);
    expected_cluster_stats.set_total_dropped_requests(2);
    expectSendMessage({expected_cluster_stats});
  }
  sendLoadReport();
}

// Validate load reporting for all clusters of the cluster manager, including clusters that are
// added and removed while reporting.
TEST_P(LoadStatsReporterImplTest, SendAllClusters) {
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _)).WillOnce(Return(&async_stream_));
  expectSendMessage({});
  createLoadStatsReporter();
  time_system_.setMonotonicTime(std::chrono::microseconds(0));

  NiceMock<MockClusterMockPrioritySet> foo_cluster;
  foo_cluster.info_->name_ = "foo";
  MockHostSet& foo_host_set = *foo_cluster.prioritySet().getMockHostSet(0);
  ::envoy::config::core::v3::Locality locality;
  locality.set_region("mars");
  HostSharedPtr foo_host = makeTestHost("foo_host", locality);
  foo_host_set.hosts_per_locality_ = makeHostsPerLocality({{foo_host}});
  NiceMock<MockClusterMockPrioritySet> bar_cluster;
  bar_cluster.info_->name_ = "bar";
  setActiveClusters({foo_cluster, bar_cluster});
  deliverLoadStatsResponse({}, false, /*send_all_clusters=*/true);

  // Both clusters are tracked and reported.
  addStats(foo_cluster, foo_host, 1.0);
  bar_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(2);
  time_system_.setMonotonicTime(std::chrono::microseconds(1));
  {
    envoy::config::endpoint::v3::ClusterStats foo_cluster_stats = makeClusterStats("foo", 1);
    auto* expected_locality_stats = foo_cluster_stats.add_upstream_locality_stats();
    expected_locality_stats->mutable_locality()->set_region("mars");
    expected_locality_stats->set_total_successful_requests(1);
    expected_locality_stats->set_total_issued_requests(1);
    addStatExpectation(expected_locality_stats, "metric_a", 1, 1.0);
    envoy::config::endpoint::v3::ClusterStats bar_cluster_stats = makeClusterStats("bar", 1);
    bar_cluster_stats.set_total_dropped_requests(2);
    expectSendMessage({foo_cluster_stats, bar_cluster_stats});
  }
  sendLoadReport();

  // bar is removed and baz is added, with load that is latched away when baz starts being
  // tracked right after the next report. baz is not reported before it is tracked.
  NiceMock<MockClusterMockPrioritySet> baz_cluster;
  baz_cluster.info_->name_ = "baz";
  baz_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(3);
  setActiveClusters({foo_cluster, baz_cluster});
  time_system_.setMonotonicTime(std::chrono::microseconds(2));
  expectHostsVisited(foo_host_set, false);
  expectSendMessage({makeClusterStats("foo", 1)});
  sendLoadReport();
  verifyHostVisitExpectations();

  // baz is now tracked and only the load since then is reported.
  // bar is re-added to the cluster manager with load incurred while it was absent: because bar was
  // erased from clusters_ at t=2, it is not reported at t=3, and its pre-tracking load is latched
  // away when startLoadReportPeriod() starts tracking it again at t=3.
  bar_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(5);
  setActiveClusters({foo_cluster, bar_cluster, baz_cluster});
  baz_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(4);
  time_system_.setMonotonicTime(std::chrono::microseconds(3));
  {
    envoy::config::endpoint::v3::ClusterStats baz_cluster_stats = makeClusterStats("baz", 1);
    baz_cluster_stats.set_total_dropped_requests(4);
    expectSendMessage({makeClusterStats("foo", 1), baz_cluster_stats});
  }
  sendLoadReport();

  // bar is now tracked again from t=3, with only load since t=3 reported.
  bar_cluster.info_->load_report_stats_.upstream_rq_dropped_.add(6);
  time_system_.setMonotonicTime(std::chrono::microseconds(4));
  {
    envoy::config::endpoint::v3::ClusterStats bar_cluster_stats = makeClusterStats("bar", 1);
    bar_cluster_stats.set_total_dropped_requests(6);
    expectSendMessage({makeClusterStats("foo", 1), bar_cluster_stats, makeClusterStats("baz", 1)});
  }
  sendLoadReport();

  // Switching to an explicit list of clusters drops the clusters that are not listed.
  deliverLoadStatsResponse({"baz"});
  time_system_.setMonotonicTime(std::chrono::microseconds(5));
  ON_CALL(cm_, getActiveCluster("baz"))
      .WillByDefault(Return(OptRef<const Upstream::Cluster>(baz_cluster)));
  expectSendMessage({makeClusterStats("baz", 1)});
  sendLoadReport();
}

INSTANTIATE_TEST_SUITE_P(SkipIdleClusters, LoadStatsReporterImplTest, testing::Bool(),
                         [](const testing::TestParamInfo<bool>& info) {
                           return info.param ? "SkipIdleClusters" : "VisitAllClusters";
                         });

} // namespace
} // namespace Upstream
} // namespace Envoy
