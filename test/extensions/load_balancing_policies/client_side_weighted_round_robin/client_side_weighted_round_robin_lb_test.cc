#include <cstdint>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/upstream/load_balancer.h"

#include "source/extensions/load_balancing_policies/client_side_weighted_round_robin/client_side_weighted_round_robin_lb.h"

#include "test/extensions/load_balancing_policies/common/load_balancer_impl_base_test.h"
#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Upstream {

class ClientSideWeightedRoundRobinLoadBalancerFriend {
public:
  explicit ClientSideWeightedRoundRobinLoadBalancerFriend(
      std::shared_ptr<ClientSideWeightedRoundRobinLoadBalancer> lb,
      std::shared_ptr<ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLb> worker_lb)
      : lb_(std::move(lb)), worker_lb_(std::move(worker_lb)) {}

  HostSelectionResponse chooseHost(LoadBalancerContext* context) {
    return worker_lb_->chooseHost(context);
  }

  HostConstSharedPtr peekAnotherHost(LoadBalancerContext* context) {
    return worker_lb_->peekAnotherHost(context);
  }

  void refreshWorkerLbWithPriority(int32_t priority) { worker_lb_->refresh(priority); }

  absl::Status initialize() { return lb_->initialize(); }

  void updateWeightsOnMainThread() { lb_->updateWeightsOnMainThread(); }

  void updateWeightsOnHosts(const HostVector& hosts) { lb_->updateWeightsOnHosts(hosts); }

  static absl::optional<uint32_t>
  getClientSideWeightIfValidFromHost(const Host& host, const MonotonicTime& min_non_empty_since,
                                     const MonotonicTime& max_last_update_time) {
    return ClientSideWeightedRoundRobinLoadBalancer::getClientSideWeightIfValidFromHost(
        host, min_non_empty_since, max_last_update_time);
  }

  static double
  getUtilizationFromOrcaReport(const xds::data::orca::v3::OrcaLoadReport& orca_load_report,
                               const std::vector<std::string>& utilization_from_metric_names) {
    return ClientSideWeightedRoundRobinLoadBalancer::OrcaLoadReportHandler::
        getUtilizationFromOrcaReport(orca_load_report, utilization_from_metric_names);
  }

  static absl::StatusOr<uint32_t>
  calculateWeightFromOrcaReport(const xds::data::orca::v3::OrcaLoadReport& orca_load_report,
                                const std::vector<std::string>& utilization_from_metric_names,
                                double error_utilization_penalty) {
    return ClientSideWeightedRoundRobinLoadBalancer::OrcaLoadReportHandler::
        calculateWeightFromOrcaReport(orca_load_report, utilization_from_metric_names,
                                      error_utilization_penalty);
  }

  absl::Status updateClientSideDataFromOrcaLoadReport(
      const xds::data::orca::v3::OrcaLoadReport& orca_load_report,
      ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData& client_side_data) {
    Envoy::StreamInfo::MockStreamInfo mock_stream_info;
    return client_side_data.onOrcaLoadReport(orca_load_report, mock_stream_info);
  }

  void setHostClientSideWeight(HostSharedPtr& host, uint32_t weight,
                               long long non_empty_since_seconds,
                               long long last_update_time_seconds) {
    auto client_side_data =
        std::make_unique<ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData>(
            lb_->report_handler_, weight, /*non_empty_since=*/
            MonotonicTime(std::chrono::seconds(non_empty_since_seconds)),
            /*last_update_time=*/
            MonotonicTime(std::chrono::seconds(last_update_time_seconds)));
    host->setLbPolicyData(std::move(client_side_data));
  }

  ClientSideWeightedRoundRobinLoadBalancer::OrcaLoadReportHandlerSharedPtr orcaLoadReportHandler() {
    return lb_->report_handler_;
  }

private:
  std::shared_ptr<ClientSideWeightedRoundRobinLoadBalancer> lb_;
  std::shared_ptr<ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLb> worker_lb_;
};

namespace {

class ClientSideWeightedRoundRobinLoadBalancerTest : public LoadBalancerTestBase {
public:
  void init(bool need_local_cluster, bool locality_weight_aware = false) {
    if (need_local_cluster) {
      local_priority_set_ = std::make_shared<PrioritySetImpl>();
      local_priority_set_->getOrCreateHostSet(0);
    }

    if (locality_weight_aware) {
      common_config_.mutable_locality_weighted_lb_config();
    }

    client_side_weighted_round_robin_config_.mutable_blackout_period()->set_seconds(10);
    client_side_weighted_round_robin_config_.mutable_weight_expiration_period()->set_seconds(180);
    client_side_weighted_round_robin_config_.mutable_weight_update_period()->set_seconds(1);
    client_side_weighted_round_robin_config_.mutable_error_utilization_penalty()->set_value(0.1);
    client_side_weighted_round_robin_config_.mutable_metric_names_for_computing_utilization()->Add(
        "metric1");
    client_side_weighted_round_robin_config_.mutable_metric_names_for_computing_utilization()->Add(
        "metric2");

    EXPECT_CALL(mock_tls_, allocateSlot());
    lb_ = std::make_shared<ClientSideWeightedRoundRobinLoadBalancerFriend>(
        std::make_shared<ClientSideWeightedRoundRobinLoadBalancer>(
            lb_config_, cluster_info_, priority_set_, runtime_, random_, simTime()),
        std::make_shared<ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLb>(
            priority_set_, local_priority_set_.get(), stats_, runtime_, random_, common_config_,
            lb_config_.round_robin_overrides_, simTime(), /*tls_shim=*/absl::nullopt));

    // Initialize the thread aware load balancer from config.
    ASSERT_EQ(lb_->initialize(), absl::OkStatus());
  }

  // Updates priority 0 with the given hosts and hosts_per_locality.
  void updateHosts(HostVectorConstSharedPtr hosts,
                   HostsPerLocalityConstSharedPtr hosts_per_locality) {
    local_priority_set_->updateHosts(
        0,
        updateHostsParams(hosts, hosts_per_locality,
                          std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
        {}, empty_host_vector_, empty_host_vector_, random_.random(), absl::nullopt);
  }

  void peekThenPick(std::vector<int> picks) {
    for (auto i : picks) {
      EXPECT_EQ(hostSet().healthy_hosts_[i], lb_->peekAnotherHost(nullptr));
    }
    for (auto i : picks) {
      EXPECT_EQ(hostSet().healthy_hosts_[i], lb_->chooseHost(nullptr).host);
    }
  }

  envoy::extensions::load_balancing_policies::client_side_weighted_round_robin::v3::
      ClientSideWeightedRoundRobin client_side_weighted_round_robin_config_;

  std::shared_ptr<PrioritySetImpl> local_priority_set_;
  std::shared_ptr<ClientSideWeightedRoundRobinLoadBalancerFriend> lb_;
  HostsPerLocalityConstSharedPtr empty_locality_;
  HostVector empty_host_vector_;

  NiceMock<MockLoadBalancerContext> lb_context_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Envoy::ThreadLocal::MockInstance> mock_tls_;
  NiceMock<MockClusterInfo> cluster_info_;
  ClientSideWeightedRoundRobinLbConfig lb_config_ = ClientSideWeightedRoundRobinLbConfig(
      client_side_weighted_round_robin_config_, dispatcher_, mock_tls_);
};

//////////////////////////////////////////////////////
// These tests verify ClientSideWeightedRoundRobinLoadBalancer specific functionality.
//

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest,
       UpdateWeightsOnHostsAllHostsHaveClientSideWeights) {
  init(false);
  HostVector hosts = {
      makeTestHost(info_, "tcp://127.0.0.1:80"),
      makeTestHost(info_, "tcp://127.0.0.1:81"),
      makeTestHost(info_, "tcp://127.0.0.1:82"),
  };
  simTime().setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));
  lb_->setHostClientSideWeight(hosts[0], 40, 5, 10);
  lb_->setHostClientSideWeight(hosts[1], 41, 5, 10);
  lb_->setHostClientSideWeight(hosts[2], 42, 5, 10);
  // Setting client side weights should not change the host weights.
  EXPECT_EQ(hosts[0]->weight(), 1);
  EXPECT_EQ(hosts[1]->weight(), 1);
  EXPECT_EQ(hosts[2]->weight(), 1);
  // Update weights on hosts.
  lb_->updateWeightsOnHosts(hosts);
  // All hosts have client side weights, so the weights should be updated.
  EXPECT_EQ(hosts[0]->weight(), 40);
  EXPECT_EQ(hosts[1]->weight(), 41);
  EXPECT_EQ(hosts[2]->weight(), 42);
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, UpdateWeightsOneHostHasClientSideWeight) {
  init(false);
  HostVector hosts = {
      makeTestHost(info_, "tcp://127.0.0.1:80"),
      makeTestHost(info_, "tcp://127.0.0.1:81"),
      makeTestHost(info_, "tcp://127.0.0.1:82"),
  };
  simTime().setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));
  // Set client side weight for one host.
  lb_->setHostClientSideWeight(hosts[0], 42, 5, 10);
  // Setting client side weights should not change the host weights.
  EXPECT_EQ(hosts[0]->weight(), 1);
  EXPECT_EQ(hosts[1]->weight(), 1);
  EXPECT_EQ(hosts[2]->weight(), 1);
  // Update weights on hosts.
  lb_->updateWeightsOnHosts(hosts);
  // Only one host has client side weight, other hosts get the median weight.
  EXPECT_EQ(hosts[0]->weight(), 42);
  EXPECT_EQ(hosts[1]->weight(), 42);
  EXPECT_EQ(hosts[2]->weight(), 42);
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, UpdateWeightsDefaultIsOddMedianWeight) {
  init(false);
  HostVector hosts = {
      makeTestHost(info_, "tcp://127.0.0.1:80"), makeTestHost(info_, "tcp://127.0.0.1:81"),
      makeTestHost(info_, "tcp://127.0.0.1:82"), makeTestHost(info_, "tcp://127.0.0.1:83"),
      makeTestHost(info_, "tcp://127.0.0.1:84"),
  };
  simTime().setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));
  // Set client side weight for first three hosts.
  lb_->setHostClientSideWeight(hosts[0], 5, 5, 10);
  lb_->setHostClientSideWeight(hosts[1], 42, 5, 10);
  lb_->setHostClientSideWeight(hosts[2], 5000, 5, 10);
  // Setting client side weights should not change the host weights.
  EXPECT_EQ(hosts[0]->weight(), 1);
  EXPECT_EQ(hosts[1]->weight(), 1);
  EXPECT_EQ(hosts[2]->weight(), 1);
  EXPECT_EQ(hosts[3]->weight(), 1);
  EXPECT_EQ(hosts[4]->weight(), 1);
  // Update weights on hosts.
  lb_->updateWeightsOnHosts(hosts);
  // First three hosts have client side weight, other hosts get the median
  // weight.
  EXPECT_EQ(hosts[0]->weight(), 5);
  EXPECT_EQ(hosts[1]->weight(), 42);
  EXPECT_EQ(hosts[2]->weight(), 5000);
  EXPECT_EQ(hosts[3]->weight(), 42);
  EXPECT_EQ(hosts[4]->weight(), 42);
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, UpdateWeightsDefaultIsEvenMedianWeight) {
  init(false);
  HostVector hosts = {
      makeTestHost(info_, "tcp://127.0.0.1:80"), makeTestHost(info_, "tcp://127.0.0.1:81"),
      makeTestHost(info_, "tcp://127.0.0.1:82"), makeTestHost(info_, "tcp://127.0.0.1:83"),
      makeTestHost(info_, "tcp://127.0.0.1:84"),
  };
  simTime().setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));
  // Set client side weight for first two hosts.
  lb_->setHostClientSideWeight(hosts[0], 5, 5, 10);
  lb_->setHostClientSideWeight(hosts[1], 42, 5, 10);
  // Setting client side weights should not change the host weights.
  EXPECT_EQ(hosts[0]->weight(), 1);
  EXPECT_EQ(hosts[1]->weight(), 1);
  EXPECT_EQ(hosts[2]->weight(), 1);
  EXPECT_EQ(hosts[3]->weight(), 1);
  EXPECT_EQ(hosts[4]->weight(), 1);
  // Update weights on hosts.
  lb_->updateWeightsOnHosts(hosts);
  // First two hosts have client side weight, other hosts get the median
  // weight which is average of weights of first two hosts.
  EXPECT_EQ(hosts[0]->weight(), 5);
  EXPECT_EQ(hosts[1]->weight(), 42);
  EXPECT_EQ(hosts[2]->weight(), 23);
  EXPECT_EQ(hosts[3]->weight(), 23);
  EXPECT_EQ(hosts[4]->weight(), 23);
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, ChooseHostWithClientSideWeights) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  hostSet().healthy_hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:80"),
      makeTestHost(info_, "tcp://127.0.0.1:81"),
      makeTestHost(info_, "tcp://127.0.0.1:82"),
  };
  hostSet().hosts_ = hostSet().healthy_hosts_;
  init(false);

  hostSet().runCallbacks({}, {});
  simTime().setMonotonicTime(MonotonicTime(std::chrono::seconds(5)));
  Envoy::StreamInfo::MockStreamInfo mock_stream_info;
  for (const auto& host_ptr : hostSet().hosts_) {
    HostConstSharedPtr host = lb_->chooseHost(&lb_context_).host;
    // Hosts have equal weights, so chooseHost returns the current host.
    ASSERT_EQ(host, host_ptr);
    // Invoke the callback with an Orca load report.
    xds::data::orca::v3::OrcaLoadReport orca_load_report;
    orca_load_report.set_rps_fractional(1000);
    orca_load_report.set_application_utilization(0.5);
    EXPECT_EQ(host->lbPolicyData()->onOrcaLoadReport(orca_load_report, mock_stream_info),
              absl::OkStatus());
    EXPECT_EQ(host->weight(), 1);
  }
  // Update weights on hosts.
  simTime().setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));
  lb_->updateWeightsOnMainThread();
  // All hosts have client side weights, so the weights should be updated.
  for (const auto& host_ptr : hostSet().hosts_) {
    EXPECT_EQ(host_ptr->weight(), 2000);
  }
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, RefreshWorkerLbWithPriority) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  hostSet().healthy_hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:80"),
      makeTestHost(info_, "tcp://127.0.0.1:81"),
      makeTestHost(info_, "tcp://127.0.0.1:82"),
  };
  hostSet().hosts_ = hostSet().healthy_hosts_;
  init(false);

  hostSet().runCallbacks({}, {});
  // Refresh worker LB with priority 42 which does not exist, expect no crash.
  lb_->refreshWorkerLbWithPriority(42);
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, ProcessOrcaLoadReport_FirstReport) {
  init(false);
  simTime().setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));

  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.set_rps_fractional(1000);
  orca_load_report.set_application_utilization(0.5);

  auto client_side_data =
      std::make_shared<ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData>(
          lb_->orcaLoadReportHandler());
  EXPECT_EQ(lb_->updateClientSideDataFromOrcaLoadReport(orca_load_report, *client_side_data),
            absl::OkStatus());
  // First report, so non_empty_since_ is updated.
  EXPECT_EQ(client_side_data->non_empty_since_.load(), MonotonicTime(std::chrono::seconds(30)));
  // last_update_time_ is updated.
  EXPECT_EQ(client_side_data->last_update_time_.load(), MonotonicTime(std::chrono::seconds(30)));
  // weight_ is calculated based on the Orca report.
  EXPECT_EQ(client_side_data->weight_.load(), 2000);
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, ProcessOrcaLoadReport_Update) {
  init(false);
  simTime().setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));

  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.set_rps_fractional(1000);
  orca_load_report.set_application_utilization(0.5);

  auto client_side_data =
      std::make_shared<ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData>(
          lb_->orcaLoadReportHandler(), 42,
          /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
          /*last_update_time=*/MonotonicTime(std::chrono::seconds(10)));
  EXPECT_EQ(lb_->updateClientSideDataFromOrcaLoadReport(orca_load_report, *client_side_data),
            absl::OkStatus());
  // Not a first report, so non_empty_since_ is not updated.
  EXPECT_EQ(client_side_data->non_empty_since_.load(), MonotonicTime(std::chrono::seconds(1)));
  // last_update_time_ is updated.
  EXPECT_EQ(client_side_data->last_update_time_.load(), MonotonicTime(std::chrono::seconds(30)));
  // weight_ is recalculated based on the Orca report.
  EXPECT_EQ(client_side_data->weight_.load(), 2000);
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, ProcessOrcaLoadReport_UpdateWithInvalidQps) {
  init(false);
  simTime().setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));

  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  // QPS is 0, so the report is invalid.
  orca_load_report.set_rps_fractional(0);
  orca_load_report.set_application_utilization(0.5);

  auto client_side_data =
      std::make_shared<ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData>(
          lb_->orcaLoadReportHandler(), 42,
          /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
          /*last_update_time=*/MonotonicTime(std::chrono::seconds(10)));
  EXPECT_EQ(lb_->updateClientSideDataFromOrcaLoadReport(orca_load_report, *client_side_data),
            absl::InvalidArgumentError("QPS must be positive"));
  // None of the client side data is updated.
  EXPECT_EQ(client_side_data->non_empty_since_.load(), MonotonicTime(std::chrono::seconds(1)));
  EXPECT_EQ(client_side_data->last_update_time_.load(), MonotonicTime(std::chrono::seconds(10)));
  EXPECT_EQ(client_side_data->weight_.load(), 42);
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, SlowStartConfig_RampUp) {
  // Configure slow start via overrides.
  auto* slow = lb_config_.round_robin_overrides_.mutable_slow_start_config();
  slow->mutable_slow_start_window()->set_seconds(60);
  slow->mutable_min_weight_percent()->set_value(0.35);
  // aggression left default (1.0).

  // Create first host, initialize LB, then later add second host to trigger slow start.
  auto h1 = makeTestHost(info_, "tcp://127.0.0.1:80", 1 /*weight*/);
  hostSet().healthy_hosts_ = {h1};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  init(false);
  hostSet().runCallbacks({}, {});

  // Advance time to simulate existing steady-state.
  simTime().advanceTimeWait(std::chrono::seconds(12));

  // Add second host now; it should be in slow start window initially.
  auto h2 = makeTestHost(info_, "tcp://127.0.0.1:81", 1 /*weight*/);
  hostSet().healthy_hosts_.push_back(h2);
  hostSet().hosts_ = hostSet().healthy_hosts_;
  hostSet().runCallbacks({}, {});

  // Expect bias towards h1 initially because h2 is dampened by min_weight_percent=0.35.
  // Perform 7 picks and expect about 5:2 in favor of h1 (tolerance via exact sequence in RR tests
  // is brittle; instead check counts with small tolerance).
  size_t picks1 = 70; // scale up to reduce flakiness
  size_t h1_count = 0, h2_count = 0;
  for (size_t i = 0; i < picks1; ++i) {
    auto chosen = lb_->chooseHost(nullptr).host;
    if (chosen == h1)
      ++h1_count;
    else if (chosen == h2)
      ++h2_count;
    else
      FAIL();
  }
  // Expect h1 to be chosen noticeably more often (ratio ~ (1):(0.35)).
  EXPECT_GT(h1_count, h2_count);

  // Advance time so that time factor ~0.5 (30/60) dominates over min 0.35.
  simTime().advanceTimeWait(std::chrono::seconds(18));
  hostSet().runCallbacks({}, {});

  h1_count = 0;
  h2_count = 0;
  for (size_t i = 0; i < picks1; ++i) {
    auto chosen = lb_->chooseHost(nullptr).host;
    if (chosen == h1)
      ++h1_count;
    else if (chosen == h2)
      ++h2_count;
    else
      FAIL();
  }
  // Now expect closer to 2:1 ratio in favor of h1; i.e., still h1 > h2.
  EXPECT_GT(h1_count, h2_count);

  // Advance time beyond slow start window so both hosts equal (1:1).
  simTime().advanceTimeWait(std::chrono::seconds(45));
  hostSet().runCallbacks({}, {});

  h1_count = 0;
  h2_count = 0;
  for (size_t i = 0; i < picks1; ++i) {
    auto chosen = lb_->chooseHost(nullptr).host;
    if (chosen == h1)
      ++h1_count;
    else if (chosen == h2)
      ++h2_count;
    else
      FAIL();
  }
  // Expect approximately equal selection; allow 30% tolerance.
  double final_ratio =
      static_cast<double>(h1_count) / static_cast<double>(h2_count == 0 ? 1 : h2_count);
  EXPECT_GT(final_ratio, 0.7);
  EXPECT_LT(final_ratio, 1.3);
}

INSTANTIATE_TEST_SUITE_P(PrimaryOrFailoverAndLegacyOrNew,
                         ClientSideWeightedRoundRobinLoadBalancerTest,
                         ::testing::Values(LoadBalancerTestParam{true},
                                           LoadBalancerTestParam{false}));

// Ensure that when no hosts have client-side data yet, updateWeightsOnHosts traverses the
// default-weight path (weights list empty, default=1) without altering host weights.
TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, UpdateWeights_AllDefault_NoClientData) {
  init(false);
  HostVector hosts = {
      makeTestHost(info_, "tcp://127.0.0.1:80"),
      makeTestHost(info_, "tcp://127.0.0.1:81"),
  };
  // Baseline weights are 1.
  EXPECT_EQ(hosts[0]->weight(), 1);
  EXPECT_EQ(hosts[1]->weight(), 1);

  // Call the helper to update weights on hosts without any client-side LB policy data.
  // This should take the codepath computing default_weight=1 and leave weights unchanged.
  lb_->updateWeightsOnHosts(hosts);

  EXPECT_EQ(hosts[0]->weight(), 1);
  EXPECT_EQ(hosts[1]->weight(), 1);
}

TEST(ClientSideWeightedRoundRobinConfigTest, SlowStartConfigPropagatesToOverrides) {
  // Build proto with slow start config set.
  envoy::extensions::load_balancing_policies::client_side_weighted_round_robin::v3::
      ClientSideWeightedRoundRobin proto;
  proto.mutable_slow_start_config()->mutable_slow_start_window()->set_seconds(15);
  proto.mutable_slow_start_config()->mutable_min_weight_percent()->set_value(0.25);

  // Construct typed config and validate that Round Robin overrides carry slow start config.
  NiceMock<Event::MockDispatcher> dispatcher;
  ThreadLocal::MockInstance tls;
  ClientSideWeightedRoundRobinLbConfig typed(proto, dispatcher, tls);
  EXPECT_TRUE(typed.round_robin_overrides_.has_slow_start_config());
  EXPECT_EQ(typed.round_robin_overrides_.slow_start_config().slow_start_window().seconds(), 15);
  EXPECT_DOUBLE_EQ(typed.round_robin_overrides_.slow_start_config().min_weight_percent().value(),
                   0.25);
}

// Unit tests for ClientSideWeightedRoundRobinLoadBalancer implementation.

TEST(ClientSideWeightedRoundRobinLoadBalancerTest,
     GetClientSideWeightIfValidFromHost_NoClientSideData) {
  NiceMock<Envoy::Upstream::MockHost> host;
  EXPECT_FALSE(ClientSideWeightedRoundRobinLoadBalancerFriend::getClientSideWeightIfValidFromHost(
      host, MonotonicTime::min(), MonotonicTime::max()));
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, GetClientSideWeightIfValidFromHost_TooRecent) {
  NiceMock<Envoy::Upstream::MockHost> host;
  host.lb_policy_data_ =
      std::make_unique<ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData>(
          nullptr, 42, /*non_empty_since=*/MonotonicTime(std::chrono::seconds(5)),
          /*last_update_time=*/MonotonicTime(std::chrono::seconds(10)));
  // Non empty since is too recent (5 > 2).
  EXPECT_FALSE(ClientSideWeightedRoundRobinLoadBalancerFriend::getClientSideWeightIfValidFromHost(
      host,
      /*min_non_empty_since=*/MonotonicTime(std::chrono::seconds(2)),
      /*max_last_update_time=*/MonotonicTime(std::chrono::seconds(8))));
  // non_empty_since_ is not updated.
  EXPECT_EQ(
      host.typedLbPolicyData<ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData>()
          ->non_empty_since_.load(),
      MonotonicTime(std::chrono::seconds(5)));
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, GetClientSideWeightIfValidFromHost_TooStale) {
  NiceMock<Envoy::Upstream::MockHost> host;
  host.lb_policy_data_ =
      std::make_unique<ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData>(
          nullptr, 42, /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
          /*last_update_time=*/MonotonicTime(std::chrono::seconds(7)));
  // Last update time is too stale (7 < 8).
  EXPECT_FALSE(ClientSideWeightedRoundRobinLoadBalancerFriend::getClientSideWeightIfValidFromHost(
      host,
      /*min_non_empty_since=*/MonotonicTime(std::chrono::seconds(2)),
      /*max_last_update_time=*/MonotonicTime(std::chrono::seconds(8))));
  // Also resets the non_empty_since_ time.
  EXPECT_EQ(
      host.typedLbPolicyData<ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData>()
          ->non_empty_since_.load(),
      ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData::kDefaultNonEmptySince);
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, GetClientSideWeightIfValidFromHost_Valid) {
  NiceMock<Envoy::Upstream::MockHost> host;
  host.lb_policy_data_ =
      std::make_unique<ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData>(
          nullptr, 42, /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
          /*last_update_time=*/MonotonicTime(std::chrono::seconds(10)));
  // Not empty since is not too recent (1 < 2) and last update time is not too
  // old (10 > 8).
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::getClientSideWeightIfValidFromHost(
                host,
                /*min_non_empty_since=*/MonotonicTime(std::chrono::seconds(2)),
                /*max_last_update_time=*/MonotonicTime(std::chrono::seconds(8)))
                .value(),
            42);
  // non_empty_since_ is not updated.
  EXPECT_EQ(
      host.typedLbPolicyData<ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData>()
          ->non_empty_since_.load(),
      MonotonicTime(std::chrono::seconds(1)));
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest,
     GetUtilizationFromOrcaReport_ApplicationUtilization) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.set_application_utilization(0.5);
  orca_load_report.mutable_named_metrics()->insert({"foo", 0.3});
  orca_load_report.set_cpu_utilization(0.6);
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::getUtilizationFromOrcaReport(
                orca_load_report, {"named_metrics.foo"}),
            0.5);
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, GetUtilizationFromOrcaReport_NamedMetrics) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.mutable_named_metrics()->insert({"foo", 0.3});
  orca_load_report.set_cpu_utilization(0.6);
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::getUtilizationFromOrcaReport(
                orca_load_report, {"named_metrics.foo"}),
            0.3);
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, GetUtilizationFromOrcaReport_CpuUtilization) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.mutable_named_metrics()->insert({"bar", 0.3});
  orca_load_report.set_cpu_utilization(0.6);
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::getUtilizationFromOrcaReport(
                orca_load_report, {"named_metrics.foo"}),
            0.6);
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, GetUtilizationFromOrcaReport_NoUtilization) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::getUtilizationFromOrcaReport(
                orca_load_report, {"named_metrics.foo"}),
            0);
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, CalculateWeightFromOrcaReport_NoQps) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::calculateWeightFromOrcaReport(
                orca_load_report, {"named_metrics.foo"}, 0.0)
                .status(),
            absl::InvalidArgumentError("QPS must be positive"));
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, CalculateWeightFromOrcaReport_NoUtilization) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.set_rps_fractional(1000);
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::calculateWeightFromOrcaReport(
                orca_load_report, {"named_metrics.foo"}, 0.0)
                .status(),
            absl::InvalidArgumentError("Utilization must be positive"));
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest,
     CalculateWeightFromOrcaReport_ValidQpsAndUtilization) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.set_rps_fractional(1000);
  orca_load_report.set_application_utilization(0.5);
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::calculateWeightFromOrcaReport(
                orca_load_report, {"named_metrics.foo"}, 0.0)
                .value(),
            2000);
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, CalculateWeightFromOrcaReport_MaxWeight) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  // High QPS and low utilization.
  orca_load_report.set_rps_fractional(10000000000000L);
  orca_load_report.set_application_utilization(0.0000001);
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::calculateWeightFromOrcaReport(
                orca_load_report, {"named_metrics.foo"}, 0.0)
                .value(),
            /*std::numeric_limits<uint32_t>::max() = */ 4294967295);
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest,
     CalculateWeightFromOrcaReport_ValidNoErrorPenalty) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.set_rps_fractional(1000);
  orca_load_report.set_eps(100);
  orca_load_report.set_application_utilization(0.5);
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::calculateWeightFromOrcaReport(
                orca_load_report, {"named_metrics.foo"}, 0.0)
                .value(),
            2000);
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest,
     CalculateWeightFromOrcaReport_ValidWithErrorPenalty) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.set_rps_fractional(1000);
  orca_load_report.set_eps(100);
  orca_load_report.set_application_utilization(0.5);
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::calculateWeightFromOrcaReport(
                orca_load_report, {"named_metrics.foo"}, 2.0)
                .value(),
            1428);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
