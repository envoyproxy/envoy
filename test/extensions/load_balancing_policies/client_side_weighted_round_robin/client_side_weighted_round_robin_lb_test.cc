#include <cstdint>
#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/upstream/load_balancer.h"

#include "source/extensions/load_balancing_policies/client_side_weighted_round_robin/client_side_weighted_round_robin_lb.h"

#include "test/extensions/load_balancing_policies/common/load_balancer_impl_base_test.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Upstream {

// Friend ClientSideWeightedRoundRobinLoadBalancer to provide access to private methods.
class ClientSideWeightedRoundRobinLoadBalancerFriend {
public:
  explicit ClientSideWeightedRoundRobinLoadBalancerFriend(
      std::shared_ptr<ClientSideWeightedRoundRobinLoadBalancer> lb,
      std::shared_ptr<ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLb> worker_lb)
      : lb_(std::move(lb)), worker_lb_(std::move(worker_lb)) {}

  ~ClientSideWeightedRoundRobinLoadBalancerFriend() = default;

  HostConstSharedPtr chooseHost(LoadBalancerContext* context) {
    return worker_lb_->chooseHost(context);
  }

  HostConstSharedPtr peekAnotherHost(LoadBalancerContext* context) {
    return worker_lb_->peekAnotherHost(context);
  }

  absl::Status initialize() { return lb_->initialize(); }

  void updateWeightsOnMainThread() { lb_->updateWeightsOnMainThread(); }

  void updateWeightsOnHosts(const HostVector& hosts) { lb_->updateWeightsOnHosts(hosts); }

  static void addClientSideLbPolicyDataToHosts(const HostVector& hosts) {
    ClientSideWeightedRoundRobinLoadBalancer::addClientSideLbPolicyDataToHosts(hosts);
  }

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
    return worker_lb_->orca_load_report_handler_->updateClientSideDataFromOrcaLoadReport(
        orca_load_report, client_side_data);
  }

  absl::Status onOrcaLoadReport(const OrcaLoadReportProto& orca_load_report,
                                const HostDescription& host_description) {
    return worker_lb_->orca_load_report_handler_->onOrcaLoadReport(orca_load_report,
                                                                   host_description);
  }

private:
  std::shared_ptr<ClientSideWeightedRoundRobinLoadBalancer> lb_;
  std::shared_ptr<ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLb> worker_lb_;
};

namespace {

using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

void setHostClientSideWeight(HostSharedPtr& host, uint32_t weight,
                             long long non_empty_since_seconds,
                             long long last_update_time_seconds) {
  auto client_side_data =
      std::make_unique<ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData>(
          weight, /*non_empty_since=*/
          MonotonicTime(std::chrono::seconds(non_empty_since_seconds)),
          /*last_update_time=*/
          MonotonicTime(std::chrono::seconds(last_update_time_seconds)));
  host->setLbPolicyData(std::move(client_side_data));
}

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

    lb_ = std::make_shared<ClientSideWeightedRoundRobinLoadBalancerFriend>(
        std::make_shared<ClientSideWeightedRoundRobinLoadBalancer>(
            lb_config_, cluster_info_, priority_set_, runtime_, random_, simTime()),
        std::make_shared<ClientSideWeightedRoundRobinLoadBalancer::WorkerLocalLb>(
            priority_set_, local_priority_set_.get(), stats_, runtime_, random_, common_config_,
            lb_config_, simTime()));

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
      EXPECT_EQ(hostSet().healthy_hosts_[i], lb_->chooseHost(nullptr));
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
  NiceMock<MockClusterInfo> cluster_info_;
  ClientSideWeightedRoundRobinLbConfig lb_config_ =
      ClientSideWeightedRoundRobinLbConfig(client_side_weighted_round_robin_config_, dispatcher_);
};

//////////////////////////////////////////////////////
// These tests verify ClientSideWeightedRoundRobinLoadBalancer specific functionality.
//

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest,
       UpdateWeightsOnHostsAllHostsHaveClientSideWeights) {
  init(false);
  HostVector hosts = {
      makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
      makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
      makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
  };
  simTime().setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));
  setHostClientSideWeight(hosts[0], 40, 5, 10);
  setHostClientSideWeight(hosts[1], 41, 5, 10);
  setHostClientSideWeight(hosts[2], 42, 5, 10);
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
      makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
      makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
      makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
  };
  simTime().setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));
  // Set client side weight for one host.
  setHostClientSideWeight(hosts[0], 42, 5, 10);
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

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, UpdateWeightsDefaultIsMedianWeight) {
  init(false);
  HostVector hosts = {
      makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
      makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
      makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
      makeTestHost(info_, "tcp://127.0.0.1:83", simTime()),
      makeTestHost(info_, "tcp://127.0.0.1:84", simTime()),
  };
  simTime().setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));
  // Set client side weight for first three hosts.
  setHostClientSideWeight(hosts[0], 5, 5, 10);
  setHostClientSideWeight(hosts[1], 42, 5, 10);
  setHostClientSideWeight(hosts[2], 5000, 5, 10);
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

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, ChooseHostWithClientSideWeights) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  hostSet().healthy_hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
      makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
      makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
  };
  hostSet().hosts_ = hostSet().healthy_hosts_;
  init(false);

  hostSet().runCallbacks({}, {});
  simTime().setMonotonicTime(MonotonicTime(std::chrono::seconds(5)));
  for (const auto& host_ptr : hostSet().hosts_) {
    // chooseHost calls setOrcaLoadReportCallbacks.
    std::weak_ptr<LoadBalancerContext::OrcaLoadReportCallbacks> weak_orca_load_report_callbacks;
    EXPECT_CALL(lb_context_, setOrcaLoadReportCallbacks(_))
        .WillOnce(
            Invoke([&](std::weak_ptr<LoadBalancerContext::OrcaLoadReportCallbacks> callbacks) {
              weak_orca_load_report_callbacks = callbacks;
            }));
    HostConstSharedPtr host = lb_->chooseHost(&lb_context_);
    // Hosts have equal weights, so chooseHost returns the current host.
    ASSERT_EQ(host, host_ptr);
    // Invoke the callback with an Orca load report.
    xds::data::orca::v3::OrcaLoadReport orca_load_report;
    orca_load_report.set_rps_fractional(1000);
    orca_load_report.set_application_utilization(0.5);
    // Orca load report callback does NOT change the host weight.
    auto orca_load_report_callbacks = weak_orca_load_report_callbacks.lock();
    ASSERT_NE(orca_load_report_callbacks, nullptr);
    EXPECT_EQ(orca_load_report_callbacks->onOrcaLoadReport(orca_load_report, *host.get()),
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

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, ProcessOrcaLoadReport_FirstReport) {
  init(false);
  simTime().setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));

  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.set_rps_fractional(1000);
  orca_load_report.set_application_utilization(0.5);

  auto client_side_data =
      std::make_shared<ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData>();
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
          42, /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
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
          42, /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
          /*last_update_time=*/MonotonicTime(std::chrono::seconds(10)));
  EXPECT_EQ(lb_->updateClientSideDataFromOrcaLoadReport(orca_load_report, *client_side_data),
            absl::InvalidArgumentError("QPS must be positive"));
  // None of the client side data is updated.
  EXPECT_EQ(client_side_data->non_empty_since_.load(), MonotonicTime(std::chrono::seconds(1)));
  EXPECT_EQ(client_side_data->last_update_time_.load(), MonotonicTime(std::chrono::seconds(10)));
  EXPECT_EQ(client_side_data->weight_.load(), 42);
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, OnOrcaLoadReport_NoClientSideData) {
  init(false);
  simTime().setMonotonicTime(MonotonicTime(std::chrono::seconds(30)));

  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  // QPS is 0, so the report is invalid.
  orca_load_report.set_rps_fractional(0);
  orca_load_report.set_application_utilization(0.5);

  auto host = makeTestHost(info_, "tcp://127.0.0.1:80", simTime());

  EXPECT_EQ(lb_->onOrcaLoadReport(orca_load_report, *host.get()),
            absl::NotFoundError("Host does not have ClientSideLbPolicyData"));
}

INSTANTIATE_TEST_SUITE_P(PrimaryOrFailoverAndLegacyOrNew,
                         ClientSideWeightedRoundRobinLoadBalancerTest,
                         ::testing::Values(LoadBalancerTestParam{true},
                                           LoadBalancerTestParam{false}));

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
          42, /*non_empty_since=*/MonotonicTime(std::chrono::seconds(5)),
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
          42, /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
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
          42, /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
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
