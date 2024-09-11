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
      std::shared_ptr<ClientSideWeightedRoundRobinLoadBalancer> lb)
      : lb_(std::move(lb)) {}

  ~ClientSideWeightedRoundRobinLoadBalancerFriend() = default;

  HostConstSharedPtr chooseHost(LoadBalancerContext* context) { return lb_->chooseHost(context); }

  HostConstSharedPtr peekAnotherHost(LoadBalancerContext* context) {
    return lb_->peekAnotherHost(context);
  }

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
    return ClientSideWeightedRoundRobinLoadBalancer::getUtilizationFromOrcaReport(
        orca_load_report, utilization_from_metric_names);
  }

  static absl::StatusOr<uint32_t>
  calculateWeightFromOrcaReport(const xds::data::orca::v3::OrcaLoadReport& orca_load_report,
                                const std::vector<std::string>& utilization_from_metric_names,
                                double error_utilization_penalty) {
    return ClientSideWeightedRoundRobinLoadBalancer::calculateWeightFromOrcaReport(
        orca_load_report, utilization_from_metric_names, error_utilization_penalty);
  }

  absl::Status updateClientSideDataFromOrcaLoadReport(
      const xds::data::orca::v3::OrcaLoadReport& orca_load_report,
      ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData& client_side_data) {
    return lb_->updateClientSideDataFromOrcaLoadReport(orca_load_report, client_side_data);
  }

private:
  std::shared_ptr<ClientSideWeightedRoundRobinLoadBalancer> lb_;
};

namespace {

using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

void setHostClientSideWeight(HostSharedPtr& host, uint32_t weight,
                             long long non_empty_since_seconds,
                             long long last_update_time_seconds) {
  auto client_side_data =
      std::make_shared<ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData>(
          weight, /*non_empty_since=*/
          MonotonicTime(std::chrono::seconds(non_empty_since_seconds)),
          /*last_update_time=*/
          MonotonicTime(std::chrono::seconds(last_update_time_seconds)));
  host->setLbPolicyData(client_side_data);
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
    client_side_weighted_round_robin_config_.mutable_utilization_from_metric_names()->Add(
        "metric1");
    client_side_weighted_round_robin_config_.mutable_utilization_from_metric_names()->Add(
        "metric2");

    lb_ = std::make_shared<ClientSideWeightedRoundRobinLoadBalancerFriend>(
        std::make_shared<ClientSideWeightedRoundRobinLoadBalancer>(
            priority_set_, local_priority_set_.get(), stats_, runtime_, random_, common_config_,
            client_side_weighted_round_robin_config_, simTime(), dispatcher_));
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
};

//////////////////////////////////////////////////////
// These tests are copied from ../round_robin/round_robin_lb_test.cc to test common functionality.
//
// For the tests which mutate primary and failover host sets explicitly, only
// run once.
using FailoverTest = ClientSideWeightedRoundRobinLoadBalancerTest;

// Ensure if all the hosts with priority 0 unhealthy, the next priority hosts
// are used.
TEST_P(FailoverTest, BasicFailover) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  failover_host_set_.healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:82", simTime())};
  failover_host_set_.hosts_ = failover_host_set_.healthy_hosts_;
  init(false);
  // DeterminePriorityLoad is called in chooseHost.
  HealthyAndDegradedLoad priority_load{Upstream::HealthyLoad({0, 100}),
                                       Upstream::DegradedLoad({0, 0})};
  ON_CALL(lb_context_, determinePriorityLoad(_, _, _)).WillByDefault(ReturnRef(priority_load));

  EXPECT_EQ(failover_host_set_.healthy_hosts_[0], lb_->peekAnotherHost(&lb_context_));
  EXPECT_EQ(failover_host_set_.healthy_hosts_[0], lb_->chooseHost(&lb_context_));
}

// Ensure if all the hosts with priority 0 degraded, the first priority degraded
// hosts are used.
TEST_P(FailoverTest, BasicDegradedHosts) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  host_set_.degraded_hosts_ = host_set_.hosts_;
  failover_host_set_.hosts_ = failover_host_set_.healthy_hosts_;
  init(false);
  // We don't preconnect degraded hosts.
  EXPECT_EQ(nullptr, lb_->peekAnotherHost(nullptr));
  EXPECT_EQ(host_set_.degraded_hosts_[0], lb_->chooseHost(nullptr));
}

// Ensure if all the hosts with priority 0 degraded, but healthy hosts in the
// failover, the healthy hosts in the second priority are used.
TEST_P(FailoverTest, BasicFailoverDegradedHosts) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  host_set_.degraded_hosts_ = host_set_.hosts_;
  failover_host_set_.healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:82", simTime())};
  failover_host_set_.hosts_ = failover_host_set_.healthy_hosts_;
  init(false);
  EXPECT_EQ(failover_host_set_.healthy_hosts_[0], lb_->chooseHost(nullptr));
}

// Test that extending the priority set with an existing LB causes the correct
// updates.
TEST_P(FailoverTest, PriorityUpdatesWithLocalHostSet) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  failover_host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  init(false);
  // With both the primary and failover hosts unhealthy, we should select an
  // unhealthy primary host.
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Update the priority set with a new priority level P=2 and ensure the host
  // is chosen
  MockHostSet& tertiary_host_set_ = *priority_set_.getMockHostSet(2);
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info_, "tcp://127.0.0.1:82", simTime())}));
  tertiary_host_set_.hosts_ = *hosts;
  tertiary_host_set_.healthy_hosts_ = tertiary_host_set_.hosts_;
  HostVector add_hosts;
  add_hosts.push_back(tertiary_host_set_.hosts_[0]);
  tertiary_host_set_.runCallbacks(add_hosts, {});
  EXPECT_EQ(tertiary_host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Now add a healthy host in P=0 and make sure it is immediately selected.
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.runCallbacks(add_hosts, {});
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Remove the healthy host and ensure we fail back over to tertiary_host_set_
  host_set_.healthy_hosts_ = {};
  host_set_.runCallbacks({}, {});
  EXPECT_EQ(tertiary_host_set_.hosts_[0], lb_->chooseHost(nullptr));
}

// Test that extending the priority set with an existing LB causes the correct
// updates when the cluster is configured to disable on panic.
TEST_P(FailoverTest, PriorityUpdatesWithLocalHostSetDisableOnPanic) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  failover_host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  common_config_.mutable_zone_aware_lb_config()->set_fail_traffic_on_panic(true);

  init(false);
  // With both the primary and failover hosts unhealthy, we should select no
  // host.
  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));

  // Update the priority set with a new priority level P=2 and ensure the host
  // is chosen
  MockHostSet& tertiary_host_set_ = *priority_set_.getMockHostSet(2);
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info_, "tcp://127.0.0.1:82", simTime())}));
  tertiary_host_set_.hosts_ = *hosts;
  tertiary_host_set_.healthy_hosts_ = tertiary_host_set_.hosts_;
  HostVector add_hosts;
  add_hosts.push_back(tertiary_host_set_.hosts_[0]);
  tertiary_host_set_.runCallbacks(add_hosts, {});
  EXPECT_EQ(tertiary_host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Now add a healthy host in P=0 and make sure it is immediately selected.
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.runCallbacks(add_hosts, {});
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Remove the healthy host and ensure we fail back over to tertiary_host_set_
  host_set_.healthy_hosts_ = {};
  host_set_.runCallbacks({}, {});
  EXPECT_EQ(tertiary_host_set_.hosts_[0], lb_->chooseHost(nullptr));
}

// Test extending the priority set.
TEST_P(FailoverTest, ExtendPrioritiesUpdatingPrioritySet) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  failover_host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  init(true);
  // With both the primary and failover hosts unhealthy, we should select an
  // unhealthy primary host.
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Update the priority set with a new priority level P=2
  // As it has healthy hosts, it should be selected.
  MockHostSet& tertiary_host_set_ = *priority_set_.getMockHostSet(2);
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info_, "tcp://127.0.0.1:82", simTime())}));
  tertiary_host_set_.hosts_ = *hosts;
  tertiary_host_set_.healthy_hosts_ = tertiary_host_set_.hosts_;
  HostVector add_hosts;
  add_hosts.push_back(tertiary_host_set_.hosts_[0]);
  tertiary_host_set_.runCallbacks(add_hosts, {});
  EXPECT_EQ(tertiary_host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Now add a healthy host in P=0 and make sure it is immediately selected.
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.runCallbacks(add_hosts, {});
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));
}

TEST_P(FailoverTest, ExtendPrioritiesWithLocalPrioritySet) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  failover_host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  init(true);
  // With both the primary and failover hosts unhealthy, we should select an
  // unhealthy primary host.
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Update the host set with a new priority level. We should start selecting
  // hosts from that level as it has viable hosts.
  MockHostSet& tertiary_host_set_ = *priority_set_.getMockHostSet(2);
  HostVectorSharedPtr hosts2(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:84", simTime())}));
  tertiary_host_set_.hosts_ = *hosts2;
  tertiary_host_set_.healthy_hosts_ = tertiary_host_set_.hosts_;
  HostVector add_hosts;
  add_hosts.push_back(tertiary_host_set_.hosts_[0]);
  tertiary_host_set_.runCallbacks(add_hosts, {});
  EXPECT_EQ(tertiary_host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Update the local hosts. We're not doing locality based routing in this
  // test, but it should at least do no harm.
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info_, "tcp://127.0.0.1:82", simTime())}));
  updateHosts(hosts, HostsPerLocalityImpl::empty());
  EXPECT_EQ(tertiary_host_set_.hosts_[0], lb_->chooseHost(nullptr));
}

// Verifies that the number of warmed hosts is used to compute priority
// spillover.
TEST_P(FailoverTest, PrioritiesWithNotAllWarmedHosts) {
  // To begin with we set up the following:
  // P0: 1 healthy, 1 unhealthy, 1 warmed.
  // P1: 1 healthy.
  // We then expect no spillover, since P0 is still overprovisioned.
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  host_set_.healthy_hosts_ = {host_set_.hosts_[0]};
  failover_host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:82", simTime())};
  failover_host_set_.healthy_hosts_ = failover_host_set_.hosts_;
  init(true);

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));
}

// Verifies that we handle zero warmed hosts.
TEST_P(FailoverTest, PrioritiesWithZeroWarmedHosts) {
  // To begin with we set up the following:
  // P0: 2 unhealthy, 0 warmed.
  // P1: 1 healthy.
  // We then expect all the traffic to spill over to P1 since P0 has an
  // effective load of zero.
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  failover_host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:82", simTime())};
  failover_host_set_.healthy_hosts_ = failover_host_set_.hosts_;

  init(true);

  EXPECT_EQ(failover_host_set_.hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(failover_host_set_.hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(failover_host_set_.hosts_[0], lb_->chooseHost(nullptr));
}

INSTANTIATE_TEST_SUITE_P(PrimaryOrFailoverAndLegacyOrNew, FailoverTest,
                         ::testing::Values(LoadBalancerTestParam{true},
                                           LoadBalancerTestParam{false}));

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, NoHosts) {
  init(false);
  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, SingleHost) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  init(false);
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, Normal) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  init(false);

  // Make sure the round robin pattern works for peeking.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->peekAnotherHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->peekAnotherHost(nullptr));

  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));

  // Make sure that if picks get ahead of peeks, peeks resume at the next pick.
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->peekAnotherHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));

  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));

  // Change host set with no peeks in progress
  hostSet().healthy_hosts_.push_back(makeTestHost(info_, "tcp://127.0.0.1:82", simTime()));
  hostSet().hosts_.push_back(hostSet().healthy_hosts_.back());
  hostSet().runCallbacks({hostSet().healthy_hosts_.back()}, {});
  peekThenPick({2, 0, 1});
  peekThenPick({2});

  // Now peek a few extra to push the index forward, alter the host set, and
  // make sure the index is restored to 0.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->peekAnotherHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->peekAnotherHost(nullptr));

  hostSet().healthy_hosts_.push_back(makeTestHost(info_, "tcp://127.0.0.1:83", simTime()));
  hostSet().hosts_.push_back(hostSet().healthy_hosts_.back());
  hostSet().runCallbacks({hostSet().healthy_hosts_.back()}, {hostSet().healthy_hosts_.front()});
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  peekThenPick({2, 3});
}

// Validate that the RNG seed influences pick order.
TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, Seed) {
  hostSet().healthy_hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
      makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
      makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
  };
  hostSet().hosts_ = hostSet().healthy_hosts_;
  EXPECT_CALL(random_, random()).WillRepeatedly(Return(1));
  init(false);
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, Locality) {
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  envoy::config::core::v3::Locality zone_c;
  zone_c.set_zone("C");

  HostVectorSharedPtr hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b),
                      makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_c)}));
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{(*hosts)[1]}, {(*hosts)[0]}, {(*hosts)[2]}});
  hostSet().hosts_ = *hosts;
  hostSet().healthy_hosts_ = *hosts;
  hostSet().healthy_hosts_per_locality_ = hosts_per_locality;
  init(false, true);
  // chooseHealthyLocality() return value determines which locality we use.
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(1));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(1));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  // When there is no locality, we RR over all available hosts.
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(absl::optional<uint32_t>()));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(absl::optional<uint32_t>()));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(absl::optional<uint32_t>()));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, DegradedLocality) {
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");

  HostVectorSharedPtr hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b),
                      makeTestHost(info_, "tcp://127.0.0.1:84", simTime(), zone_b)}));
  HostVectorSharedPtr healthy_hosts(new HostVector({(*hosts)[0]}));
  HostVectorSharedPtr degraded_hosts(new HostVector({(*hosts)[1], (*hosts)[2]}));
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{(*hosts)[0]}, {(*hosts)[1], (*hosts)[2]}});
  HostsPerLocalitySharedPtr healthy_hosts_per_locality = makeHostsPerLocality({{(*hosts)[0]}, {}});
  HostsPerLocalitySharedPtr degraded_hosts_per_locality =
      makeHostsPerLocality({{}, {(*hosts)[1], (*hosts)[2]}});

  hostSet().hosts_ = *hosts;
  hostSet().healthy_hosts_ = *healthy_hosts;
  hostSet().degraded_hosts_ = *degraded_hosts;
  hostSet().hosts_per_locality_ = hosts_per_locality;
  hostSet().healthy_hosts_per_locality_ = healthy_hosts_per_locality;
  hostSet().degraded_hosts_per_locality_ = degraded_hosts_per_locality;
  init(false, true);

  EXPECT_CALL(random_, random()).WillOnce(Return(50)).WillOnce(Return(0));
  // Since we're split between healthy and degraded, the LB should call into
  // both chooseHealthyLocality and chooseDegradedLocality.
  EXPECT_CALL(hostSet(), chooseDegradedLocality()).WillOnce(Return(1));
  EXPECT_EQ(hostSet().degraded_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, Weighted) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), 1),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), 2)};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  init(false);
  // Initial weights respected.
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  // Modify weights, we converge on new weighting after one pick cycle.
  hostSet().healthy_hosts_[0]->weight(2);
  hostSet().healthy_hosts_[1]->weight(1);
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  // Add a host, it should participate in next round of scheduling.
  hostSet().healthy_hosts_.push_back(makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), 3));
  hostSet().hosts_.push_back(hostSet().healthy_hosts_.back());
  hostSet().runCallbacks({hostSet().healthy_hosts_.back()}, {});
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
  // Remove last two hosts, add a new one with different weights.
  HostVector removed_hosts = {hostSet().hosts_[1], hostSet().hosts_[2]};
  hostSet().healthy_hosts_.pop_back();
  hostSet().healthy_hosts_.pop_back();
  hostSet().hosts_.pop_back();
  hostSet().hosts_.pop_back();
  hostSet().healthy_hosts_.push_back(makeTestHost(info_, "tcp://127.0.0.1:83", simTime(), 4));
  hostSet().hosts_.push_back(hostSet().healthy_hosts_.back());
  hostSet().healthy_hosts_[0]->weight(1);
  hostSet().runCallbacks({hostSet().healthy_hosts_.back()}, removed_hosts);
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
}

// Validate that the RNG seed influences pick order when weighted RR.
TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, WeightedSeed) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), 1),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), 2)};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  EXPECT_CALL(random_, random()).WillRepeatedly(Return(1));
  init(false);
  // Initial weights respected.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
}

// Validate that the RNG seed influences pick order when weighted RR without
// the envoy.reloadable_features.edf_lb_host_scheduler_init_fix.
// This test should be removed once
// envoy.reloadable_features.edf_lb_host_scheduler_init_fix is deprecated.
TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, WeightedSeedOldInit) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.edf_lb_host_scheduler_init_fix", "false"}});
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), 1),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), 2)};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  EXPECT_CALL(random_, random()).WillRepeatedly(Return(1));
  init(false);
  // Initial weights respected.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
}

// Validate that low weighted hosts will be chosen when the LB is created.
TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, WeightedInitializationPicksAllHosts) {
  TestScopedRuntime scoped_runtime;
  // This test should be kept just the runtime override removed once the
  // feature-flag is deprecated.
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.edf_lb_host_scheduler_init_fix", "true"}});
  // Add 3 hosts with weights {6, 3, 1}. Out of 10 refreshes with consecutive
  // random value, 6 times the first host will be chosen, 3 times the second
  // host will be chosen, and 1 time the third host will be chosen.
  hostSet().healthy_hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), 6),
      makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), 3),
      makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), 1),
  };
  hostSet().hosts_ = hostSet().healthy_hosts_;
  absl::flat_hash_map<HostConstSharedPtr, uint32_t> host_picked_count_map;
  for (const auto& host : hostSet().healthy_hosts_) {
    host_picked_count_map[host] = 0;
  }
  // When random returns x, the initialization of the lb will invoke pickAndAdd
  // x times. The test validates the result of the next call (the x+1 call).
  // Initiate 10 load-balancers with different random values.
  for (int i = 0; i < 10; ++i) {
    EXPECT_CALL(random_, random()).Times(2).WillRepeatedly(Return(i));
    ClientSideWeightedRoundRobinLoadBalancerFriend lb(
        std::make_shared<ClientSideWeightedRoundRobinLoadBalancer>(
            priority_set_, local_priority_set_.get(), stats_, runtime_, random_, common_config_,
            client_side_weighted_round_robin_config_, simTime(), dispatcher_));
    const auto& host = lb.chooseHost(nullptr);
    host_picked_count_map[host]++;
  }
  // Ensure that the number of times each host was picked is as expected.
  {
    EXPECT_EQ(host_picked_count_map[hostSet().healthy_hosts_[0]], 6);
    EXPECT_EQ(host_picked_count_map[hostSet().healthy_hosts_[1]], 3);
    EXPECT_EQ(host_picked_count_map[hostSet().healthy_hosts_[2]], 1);
  }
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, MaxUnhealthyPanic) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:83", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:84", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:85", simTime())};

  init(false);
  EXPECT_EQ(hostSet().hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().hosts_[2], lb_->chooseHost(nullptr));

  // Take the threshold back above the panic threshold.
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:83", simTime())};
  hostSet().runCallbacks({}, {});

  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));

  EXPECT_EQ(3UL, stats_.lb_healthy_panic_.value());
}

// Test that no hosts are selected when fail_traffic_on_panic is enabled.
TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, MaxUnhealthyPanicDisableOnPanic) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:83", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:84", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:85", simTime())};

  common_config_.mutable_zone_aware_lb_config()->set_fail_traffic_on_panic(true);

  init(false);
  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));

  // Take the threshold back above the panic threshold.
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:83", simTime())};
  hostSet().runCallbacks({}, {});

  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));

  EXPECT_EQ(1UL, stats_.lb_healthy_panic_.value());
}

// Ensure if the panic threshold is 0%, panic mode is disabled.
TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, DisablePanicMode) {
  hostSet().healthy_hosts_ = {};
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};

  common_config_.mutable_healthy_panic_threshold()->set_value(0);

  init(false);
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(0));
  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));
  EXPECT_EQ(0UL, stats_.lb_healthy_panic_.value());
}

// Test of host set selection with host filter
TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, HostSelectionWithFilter) {
  NiceMock<Upstream::MockLoadBalancerContext> context;

  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");

  HostVectorSharedPtr hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b)}));
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b)}});

  hostSet().hosts_ = *hosts;
  hostSet().healthy_hosts_ = *hosts;
  hostSet().healthy_hosts_per_locality_ = hosts_per_locality;

  init(false);

  // return a predicate that only accepts the first host
  EXPECT_CALL(context, shouldSelectAnotherHost(_))
      .WillRepeatedly(Invoke([&](const Host& host) -> bool {
        return host.address()->asString() != hostSet().hosts_[0]->address()->asString();
      }));
  HealthyAndDegradedLoad priority_load{Upstream::HealthyLoad({0, 0}),
                                       Upstream::DegradedLoad({0, 0})};

  if (&hostSet() == &host_set_) {
    priority_load.healthy_priority_load_ = HealthyLoad({100u, 0u});
  } else {
    priority_load.healthy_priority_load_ = HealthyLoad({0u, 100u});
  }
  EXPECT_CALL(context, determinePriorityLoad(_, _, _)).WillRepeatedly(ReturnRef(priority_load));
  EXPECT_CALL(context, hostSelectionRetryCount()).WillRepeatedly(Return(2));

  // Calling chooseHost multiple times always returns host one, since the filter
  // will reject the other host.
  EXPECT_EQ(hostSet().hosts_[0], lb_->chooseHost(&context));
  EXPECT_EQ(hostSet().hosts_[0], lb_->chooseHost(&context));
  EXPECT_EQ(hostSet().hosts_[0], lb_->chooseHost(&context));

  // By setting the retry counter to zero, we effectively disable the filter.
  EXPECT_CALL(context, hostSelectionRetryCount()).WillRepeatedly(Return(0));

  EXPECT_EQ(hostSet().hosts_[1], lb_->chooseHost(&context));
  EXPECT_EQ(hostSet().hosts_[0], lb_->chooseHost(&context));
  EXPECT_EQ(hostSet().hosts_[1], lb_->chooseHost(&context));
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, ZoneAwareSmallCluster) {
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  envoy::config::core::v3::Locality zone_c;
  zone_c.set_zone("C");

  HostVectorSharedPtr hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b),
                      makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_c)}));
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b)},
                            {makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_c)}});

  hostSet().hosts_ = *hosts;
  hostSet().healthy_hosts_ = *hosts;
  hostSet().healthy_hosts_per_locality_ = hosts_per_locality;
  common_config_.mutable_healthy_panic_threshold()->set_value(0);
  common_config_.mutable_zone_aware_lb_config()->mutable_routing_enabled()->set_value(98);
  common_config_.mutable_zone_aware_lb_config()->mutable_min_cluster_size()->set_value(7);
  init(true);
  updateHosts(hosts, hosts_per_locality);

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 0))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 98))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 7))
      .WillRepeatedly(Return(7));

  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));

  if (&hostSet() == &host_set_) {
    // Cluster size is computed once at zone aware struct regeneration point.
    EXPECT_EQ(1U, stats_.lb_zone_cluster_too_small_.value());
  } else {
    EXPECT_EQ(0U, stats_.lb_zone_cluster_too_small_.value());
    return;
  }
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 7))
      .WillRepeatedly(Return(1));
  // Trigger reload.
  updateHosts(hosts, hosts_per_locality);
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, ZoneAwareZonesMismatched) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }

  // Setup is:
  // L = local envoy
  // U = upstream host
  //
  // Zone A: 1L, 1U
  // Zone B: 1L, 0U
  // Zone C: 0L, 1U

  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  envoy::config::core::v3::Locality zone_c;
  zone_c.set_zone("C");

  HostVectorSharedPtr hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_c)}));
  // Upstream and local hosts when in zone A
  HostsPerLocalitySharedPtr upstream_hosts_per_locality_a =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_c)}});
  HostsPerLocalitySharedPtr local_hosts_per_locality_a =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b)}});

  hostSet().healthy_hosts_ = *hosts;
  hostSet().hosts_ = *hosts;
  hostSet().healthy_hosts_per_locality_ = upstream_hosts_per_locality_a;
  common_config_.mutable_healthy_panic_threshold()->set_value(50);
  common_config_.mutable_zone_aware_lb_config()->mutable_routing_enabled()->set_value(100);
  common_config_.mutable_zone_aware_lb_config()->mutable_min_cluster_size()->set_value(2);
  init(true);
  updateHosts(hosts, local_hosts_per_locality_a);

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 2))
      .WillRepeatedly(Return(2));

  // Expect zone-aware routing direct mode when in zone A
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_routing_all_directly_.value());
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(2U, stats_.lb_zone_routing_all_directly_.value());

  // Upstream and local hosts when in zone B (no local upstream in B)
  HostsPerLocalitySharedPtr upstream_hosts_per_locality_b =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_c)}},
                           true);
  HostsPerLocalitySharedPtr local_hosts_per_locality_b =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b)},
                            {makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a)}});

  hostSet().healthy_hosts_per_locality_ = upstream_hosts_per_locality_b;
  updateHosts(hosts, local_hosts_per_locality_b);

  // Expect zone-aware routing still enabled even though there is no upstream
  // host in zone B Since zone A has no residual, we should always pick an
  // upstream from zone C.
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(0)).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[1][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_routing_cross_zone_.value());
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(0)).WillOnce(Return(4999));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[1][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(2U, stats_.lb_zone_routing_cross_zone_.value());
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, ZoneAwareResidualsMismatched) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }

  // Setup is:
  // L = local envoy
  // U = upstream host
  //
  //                | expected  | legacy    |
  //                | residuals | residuals |
  // ----------------------------------------
  // Zone A: 2L, 1U | 0%        | 0%        |
  // Zone B: 2L, 0U | N/A       | N/A       |
  // Zone C: 1L, 3U | 20.83%    | 4.16%     |
  // Zone D: 1L, 3U | 20.83%    | 20.83%    |
  // Zone E: 0L, 1U | 12.50%    | 0%        |
  // ----------------------------------------
  // Totals: 6L, 8U | 54.18%    | 25%       |
  //
  // Idea is for the local cluster to be A, and for there to be residual from A.
  // The number of local and upstream zones must be the same for zone routing to
  // be enabled. Then we ensure that there are two different zones with
  // different residuals. Finally we add a zone with local hosts but no upstream
  // hosts just after the local zone to create a mismatch between the local
  // percentages and upstream percentages vectors when performing residual
  // calculations.

  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  envoy::config::core::v3::Locality zone_c;
  zone_c.set_zone("C");
  envoy::config::core::v3::Locality zone_d;
  zone_d.set_zone("D");
  envoy::config::core::v3::Locality zone_e;
  zone_e.set_zone("E");

  HostVectorSharedPtr hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_c),
                      makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_c),
                      makeTestHost(info_, "tcp://127.0.0.1:83", simTime(), zone_c),
                      makeTestHost(info_, "tcp://127.0.0.1:84", simTime(), zone_d),
                      makeTestHost(info_, "tcp://127.0.0.1:85", simTime(), zone_d),
                      makeTestHost(info_, "tcp://127.0.0.1:86", simTime(), zone_d),
                      makeTestHost(info_, "tcp://127.0.0.1:87", simTime(), zone_e)}));
  HostVectorSharedPtr local_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:0", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:1", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:2", simTime(), zone_b),
                      makeTestHost(info_, "tcp://127.0.0.1:3", simTime(), zone_b),
                      makeTestHost(info_, "tcp://127.0.0.1:4", simTime(), zone_c),
                      makeTestHost(info_, "tcp://127.0.0.1:5", simTime(), zone_d)}));

  // Local zone is zone A
  HostsPerLocalitySharedPtr upstream_hosts_per_locality =
      makeHostsPerLocality({{// Zone A
                             makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a)},
                            {// Zone C
                             makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_c),
                             makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_c),
                             makeTestHost(info_, "tcp://127.0.0.1:83", simTime(), zone_c)},
                            {// Zone D
                             makeTestHost(info_, "tcp://127.0.0.1:84", simTime(), zone_d),
                             makeTestHost(info_, "tcp://127.0.0.1:85", simTime(), zone_d),
                             makeTestHost(info_, "tcp://127.0.0.1:86", simTime(), zone_d)},
                            {// Zone E
                             makeTestHost(info_, "tcp://127.0.0.1:87", simTime(), zone_e)}});

  HostsPerLocalitySharedPtr local_hosts_per_locality =
      makeHostsPerLocality({{// Zone A
                             makeTestHost(info_, "tcp://127.0.0.1:0", simTime(), zone_a),
                             makeTestHost(info_, "tcp://127.0.0.1:1", simTime(), zone_a)},
                            {// Zone B
                             makeTestHost(info_, "tcp://127.0.0.1:2", simTime(), zone_b),
                             makeTestHost(info_, "tcp://127.0.0.1:3", simTime(), zone_b)},
                            {// Zone C
                             makeTestHost(info_, "tcp://127.0.0.1:4", simTime(), zone_c)},
                            {// Zone D
                             makeTestHost(info_, "tcp://127.0.0.1:5", simTime(), zone_d)}});

  hostSet().healthy_hosts_ = *hosts;
  hostSet().hosts_ = *hosts;
  hostSet().healthy_hosts_per_locality_ = upstream_hosts_per_locality;
  common_config_.mutable_healthy_panic_threshold()->set_value(50);
  common_config_.mutable_zone_aware_lb_config()->mutable_routing_enabled()->set_value(100);
  common_config_.mutable_zone_aware_lb_config()->mutable_min_cluster_size()->set_value(6);
  init(true);
  updateHosts(local_hosts, local_hosts_per_locality);

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(6));

  // Residual mode traffic will go directly to the upstream in A 37.5% (3/8) of
  // the time
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_routing_sampled_.value());
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(3749));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(2U, stats_.lb_zone_routing_sampled_.value());

  // The other 5/8 of the time, traffic will go to cross-zone upstreams with
  // residual capacity Zone B has no upstream hosts Zone C has a residual
  // capacity of 20.83% (20.84% with rounding error): sampled value 0-2083
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(3750)).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[1][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_routing_cross_zone_.value());
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(3750)).WillOnce(Return(2083));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[1][1], lb_->chooseHost(nullptr));
  EXPECT_EQ(2U, stats_.lb_zone_routing_cross_zone_.value());
  // Zone D has a residual capacity of 20.83% (20.84% with rounding error):
  // sampled value 2084-4167
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(2084));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[2][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(3U, stats_.lb_zone_routing_cross_zone_.value());
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(4167));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[2][1], lb_->chooseHost(nullptr));
  EXPECT_EQ(4U, stats_.lb_zone_routing_cross_zone_.value());
  // Zone E has a residual capacity of 12.5%: sampled value 4168-5417
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(4168));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[3][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(5U, stats_.lb_zone_routing_cross_zone_.value());
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(5417));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[3][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(6U, stats_.lb_zone_routing_cross_zone_.value());
  // At sampled value 5418, we loop back to the beginning of the vector and
  // select zone C again
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, ZoneAwareDifferentZoneSize) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }

  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  envoy::config::core::v3::Locality zone_c;
  zone_c.set_zone("C");

  HostVectorSharedPtr upstream_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b),
                      makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_c)}));
  HostVectorSharedPtr local_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:0", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:1", simTime(), zone_b)}));
  HostsPerLocalitySharedPtr upstream_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b)},
                            {makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_c)}});
  HostsPerLocalitySharedPtr local_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:1", simTime(), zone_b)},
                            {makeTestHost(info_, "tcp://127.0.0.1:0", simTime(), zone_a)}});

  hostSet().healthy_hosts_ = *upstream_hosts;
  hostSet().hosts_ = *upstream_hosts;
  hostSet().healthy_hosts_per_locality_ = upstream_hosts_per_locality;
  common_config_.mutable_healthy_panic_threshold()->set_value(100);
  common_config_.mutable_zone_aware_lb_config()->mutable_routing_enabled()->set_value(98);
  common_config_.mutable_zone_aware_lb_config()->mutable_min_cluster_size()->set_value(3);
  init(true);
  updateHosts(local_hosts, local_hosts_per_locality);

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 100))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 98))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 3))
      .WillRepeatedly(Return(3));

  // 2/3 of the time we should get the host in zone B
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_routing_sampled_.value());
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(6665));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(2U, stats_.lb_zone_routing_sampled_.value());

  // 1/3 of the time we should sample the residuals across zones
  // The only upstream zone with residual capacity is zone C
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(6666)).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[2][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_routing_cross_zone_.value());
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(3333));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[2][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(2U, stats_.lb_zone_routing_cross_zone_.value());
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, ZoneAwareRoutingLargeZoneSwitchOnOff) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  envoy::config::core::v3::Locality zone_c;
  zone_c.set_zone("C");
  HostVectorSharedPtr hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b),
                      makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_c)}));
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b)},
                            {makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_c)}});

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(3));

  hostSet().healthy_hosts_ = *hosts;
  hostSet().hosts_ = *hosts;
  hostSet().healthy_hosts_per_locality_ = hosts_per_locality;
  init(true);
  updateHosts(hosts, hosts_per_locality);

  // There is only one host in the given zone for zone aware routing.
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_routing_all_directly_.value());
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(2U, stats_.lb_zone_routing_all_directly_.value());

  // Disable runtime global zone routing.
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(false));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, ZoneAwareRoutingSmallZone) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  envoy::config::core::v3::Locality zone_c;
  zone_c.set_zone("C");
  HostVectorSharedPtr upstream_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_b),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_b),
                      makeTestHost(info_, "tcp://127.0.0.1:83", simTime(), zone_c),
                      makeTestHost(info_, "tcp://127.0.0.1:84", simTime(), zone_c)}));
  HostVectorSharedPtr local_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:0", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:1", simTime(), zone_b),
                      makeTestHost(info_, "tcp://127.0.0.1:2", simTime(), zone_c)}));

  HostsPerLocalitySharedPtr upstream_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_b),
                             makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_b)},
                            {makeTestHost(info_, "tcp://127.0.0.1:83", simTime(), zone_c),
                             makeTestHost(info_, "tcp://127.0.0.1:84", simTime(), zone_c)}});

  HostsPerLocalitySharedPtr local_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:0", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:1", simTime(), zone_b)},
                            {makeTestHost(info_, "tcp://127.0.0.1:2", simTime(), zone_c)}});

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(5));

  hostSet().healthy_hosts_ = *upstream_hosts;
  hostSet().hosts_ = *upstream_hosts;
  hostSet().healthy_hosts_per_locality_ = upstream_hosts_per_locality;
  init(true);
  updateHosts(local_hosts, local_hosts_per_locality);

  // There is only one host in the given zone for zone aware routing.
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(100));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_routing_sampled_.value());

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[1][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_routing_cross_zone_.value());
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, ZoneAwareNoMatchingZones) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  envoy::config::core::v3::Locality zone_c;
  zone_c.set_zone("C");
  envoy::config::core::v3::Locality zone_d;
  zone_d.set_zone("D");
  envoy::config::core::v3::Locality zone_e;
  zone_e.set_zone("E");
  envoy::config::core::v3::Locality zone_f;
  zone_f.set_zone("F");
  HostVectorSharedPtr upstream_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_d),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_e),
                      makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_f)}));
  HostVectorSharedPtr local_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:0", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:1", simTime(), zone_b),
                      makeTestHost(info_, "tcp://127.0.0.1:2", simTime(), zone_c)}));

  HostsPerLocalitySharedPtr upstream_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_d)},
                            {makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_e)},
                            {makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_f)}},
                           true);

  HostsPerLocalitySharedPtr local_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:0", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:1", simTime(), zone_b)},
                            {makeTestHost(info_, "tcp://127.0.0.1:2", simTime(), zone_c)}});

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(3));

  hostSet().healthy_hosts_ = *upstream_hosts;
  hostSet().hosts_ = *upstream_hosts;
  hostSet().healthy_hosts_per_locality_ = upstream_hosts_per_locality;
  init(true);
  updateHosts(local_hosts, local_hosts_per_locality);

  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(0)).WillOnce(Return(3332));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_routing_cross_zone_.value());
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(0)).WillOnce(Return(3333));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[1][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(2U, stats_.lb_zone_routing_cross_zone_.value());
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(0)).WillOnce(Return(6665));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[1][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(3U, stats_.lb_zone_routing_cross_zone_.value());
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(0)).WillOnce(Return(6666));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[2][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(4U, stats_.lb_zone_routing_cross_zone_.value());
  EXPECT_CALL(random_, random())
      .WillOnce(Return(0))
      .WillOnce(Return(0))
      .WillOnce(Return(9998)); // Rounding error: 3333 * 3 = 9999 != 10000
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[2][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(5U, stats_.lb_zone_routing_cross_zone_.value());
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, NoZoneAwareNotEnoughLocalZones) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");

  HostVectorSharedPtr upstream_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b)}));
  HostVectorSharedPtr local_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:0", simTime(), zone_a)}));

  HostsPerLocalitySharedPtr upstream_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b)}});

  HostsPerLocalitySharedPtr local_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:0", simTime(), zone_a)}});

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(2));

  hostSet().healthy_hosts_ = *upstream_hosts;
  hostSet().hosts_ = *upstream_hosts;
  hostSet().healthy_hosts_per_locality_ = upstream_hosts_per_locality;
  init(true);
  updateHosts(local_hosts, local_hosts_per_locality);

  EXPECT_CALL(random_, random()).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_CALL(random_, random()).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(0U, stats_.lb_zone_routing_all_directly_.value());
  EXPECT_EQ(0U, stats_.lb_zone_routing_sampled_.value());
  EXPECT_EQ(0U, stats_.lb_zone_routing_cross_zone_.value());
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, NoZoneAwareNotEnoughUpstreamZones) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");

  HostVectorSharedPtr upstream_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a)}));
  HostVectorSharedPtr local_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:0", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:1", simTime(), zone_b)}));

  HostsPerLocalitySharedPtr upstream_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a)}});

  HostsPerLocalitySharedPtr local_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:0", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:1", simTime(), zone_b)}});

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(1));

  hostSet().healthy_hosts_ = *upstream_hosts;
  hostSet().hosts_ = *upstream_hosts;
  hostSet().healthy_hosts_per_locality_ = upstream_hosts_per_locality;
  init(true);
  updateHosts(local_hosts, local_hosts_per_locality);

  EXPECT_CALL(random_, random()).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_CALL(random_, random()).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(0U, stats_.lb_zone_routing_all_directly_.value());
  EXPECT_EQ(0U, stats_.lb_zone_routing_sampled_.value());
  EXPECT_EQ(0U, stats_.lb_zone_routing_cross_zone_.value());
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, ZoneAwareEmptyLocalities) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }

  // L = local host
  // U = upstream host
  //
  // Zone A: 1L, 0U [Residual:  0.00%]
  // Zone B: 0L, 0U [Residual:    N/A]
  // Zone C: 1L, 2U [Residual:  8.33%]
  // Zone D: 1L, 0U [Residual:    N/A]
  // Zone E: 0L, 1U [Residual: 16.67%]
  // Zone F: 1L, 2U [Residual:  8.33%]
  // Zone G: --, 0U [Residual:    N/A]
  // Zone H: --, 1U [Residual: 16.67%]
  // Zone I: --, 0U [Residual:    N/A]
  //  Total: 4L, 6U [Residual: 50.00%]

  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_c;
  zone_c.set_zone("C");
  envoy::config::core::v3::Locality zone_d;
  zone_d.set_zone("D");
  envoy::config::core::v3::Locality zone_e;
  zone_e.set_zone("E");
  envoy::config::core::v3::Locality zone_f;
  zone_f.set_zone("F");
  envoy::config::core::v3::Locality zone_h;
  zone_h.set_zone("H");

  HostVectorSharedPtr upstream_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_c),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_c),
                      makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_e),
                      makeTestHost(info_, "tcp://127.0.0.1:83", simTime(), zone_f),
                      makeTestHost(info_, "tcp://127.0.0.1:84", simTime(), zone_f),
                      makeTestHost(info_, "tcp://127.0.0.1:85", simTime(), zone_h)}));
  HostVectorSharedPtr local_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:0", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:1", simTime(), zone_c),
                      makeTestHost(info_, "tcp://127.0.0.1:2", simTime(), zone_d),
                      makeTestHost(info_, "tcp://127.0.0.1:2", simTime(), zone_f)}));

  HostsPerLocalitySharedPtr upstream_hosts_per_locality =
      makeHostsPerLocality({{},
                            {},
                            {makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_c),
                             makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_c)},
                            {makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), zone_e)},
                            {makeTestHost(info_, "tcp://127.0.0.1:83", simTime(), zone_f),
                             makeTestHost(info_, "tcp://127.0.0.1:84", simTime(), zone_f)},
                            {},
                            {makeTestHost(info_, "tcp://127.0.0.1:85", simTime(), zone_h)},
                            {}});

  HostsPerLocalitySharedPtr local_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:0", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:0", simTime(), zone_c)},
                            {makeTestHost(info_, "tcp://127.0.0.1:1", simTime(), zone_d)},
                            {},
                            {makeTestHost(info_, "tcp://127.0.0.1:2", simTime(), zone_f)}});

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(3));

  hostSet().healthy_hosts_ = *upstream_hosts;
  hostSet().hosts_ = *upstream_hosts;
  hostSet().healthy_hosts_per_locality_ = upstream_hosts_per_locality;
  init(true);
  updateHosts(local_hosts, local_hosts_per_locality);

  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(0)).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[2][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_routing_cross_zone_.value());
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(0)).WillOnce(Return(832));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[2][1], lb_->chooseHost(nullptr));
  EXPECT_EQ(2U, stats_.lb_zone_routing_cross_zone_.value());

  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(0)).WillOnce(Return(833));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[3][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(3U, stats_.lb_zone_routing_cross_zone_.value());
  EXPECT_CALL(random_, random())
      .WillOnce(Return(0))
      .WillOnce(Return(0))
      .WillOnce(Return(2498)); // rounding error
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[3][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(4U, stats_.lb_zone_routing_cross_zone_.value());

  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(0)).WillOnce(Return(2499));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[4][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(5U, stats_.lb_zone_routing_cross_zone_.value());
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(0)).WillOnce(Return(3331));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[4][1], lb_->chooseHost(nullptr));
  EXPECT_EQ(6U, stats_.lb_zone_routing_cross_zone_.value());

  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(0)).WillOnce(Return(3332));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[6][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(7U, stats_.lb_zone_routing_cross_zone_.value());
  EXPECT_CALL(random_, random())
      .WillOnce(Return(0))
      .WillOnce(Return(0))
      .WillOnce(Return(4997)); // rounding error
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[6][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(8U, stats_.lb_zone_routing_cross_zone_.value());

  EXPECT_CALL(random_, random())
      .WillOnce(Return(0))
      .WillOnce(Return(0))
      .WillOnce(Return(4998)); // wrap around
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[2][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(9U, stats_.lb_zone_routing_cross_zone_.value());
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, LowPrecisionForDistribution) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  // upstream_hosts and local_hosts do not matter, zone aware routing is based
  // on per zone hosts.
  HostVectorSharedPtr upstream_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime())}));
  hostSet().healthy_hosts_ = *upstream_hosts;
  hostSet().hosts_ = *upstream_hosts;
  HostVectorSharedPtr local_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:0", simTime())}));

  std::vector<HostVector> upstream_hosts_per_locality;
  std::vector<HostVector> local_hosts_per_locality;

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(1));

  // The following host distribution with current precision should lead to the
  // no_capacity_left situation. Reuse the same host for each zone in all of the
  // structures below to reduce time test takes and this does not impact load
  // balancing logic.
  HostSharedPtr host_a = makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a);
  HostSharedPtr host_b = makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_b);
  HostVector current(45000);

  for (int i = 0; i < 45000; ++i) {
    current[i] = host_a;
  }
  local_hosts_per_locality.push_back(current);

  current.resize(55000);
  for (int i = 0; i < 55000; ++i) {
    current[i] = host_b;
  }
  local_hosts_per_locality.push_back(current);

  current.resize(44999);
  for (int i = 0; i < 44999; ++i) {
    current[i] = host_a;
  }
  upstream_hosts_per_locality.push_back(current);

  current.resize(55001);
  for (int i = 0; i < 55001; ++i) {
    current[i] = host_b;
  }
  upstream_hosts_per_locality.push_back(current);

  hostSet().healthy_hosts_per_locality_ =
      makeHostsPerLocality(std::move(upstream_hosts_per_locality));
  init(true);

  // To trigger update callback.
  auto local_hosts_per_locality_shared = makeHostsPerLocality(std::move(local_hosts_per_locality));
  updateHosts(local_hosts, local_hosts_per_locality_shared);

  // Force request out of small zone and to randomly select zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(2));
  lb_->chooseHost(nullptr);
  EXPECT_EQ(1U, stats_.lb_zone_no_capacity_left_.value());
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, NoZoneAwareRoutingOneZone) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime())}));
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:81", simTime())}});

  hostSet().healthy_hosts_ = *hosts;
  hostSet().hosts_ = *hosts;
  hostSet().healthy_hosts_per_locality_ = hosts_per_locality;
  init(true);
  updateHosts(hosts, hosts_per_locality);
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, NoZoneAwareRoutingNotHealthy) {
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  HostVectorSharedPtr hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.2:80", simTime(), zone_a)}));
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a),
                             makeTestHost(info_, "tcp://127.0.0.2:80", simTime(), zone_a)}},
                           true);

  hostSet().healthy_hosts_ = *hosts;
  hostSet().hosts_ = *hosts;
  hostSet().healthy_hosts_per_locality_ = hosts_per_locality;
  init(true);
  updateHosts(hosts, hosts_per_locality);

  // local zone has no healthy hosts, take from the all healthy hosts.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, NoZoneAwareRoutingLocalEmpty) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  HostVectorSharedPtr upstream_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b)}));
  HostVectorSharedPtr local_hosts(new HostVector({}, {}));

  HostsPerLocalitySharedPtr upstream_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b)}});
  HostsPerLocalitySharedPtr local_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:0", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:1", simTime(), zone_b)}});

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillOnce(Return(50))
      .WillOnce(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillOnce(Return(1));

  hostSet().healthy_hosts_ = *upstream_hosts;
  hostSet().hosts_ = *upstream_hosts;
  hostSet().healthy_hosts_per_locality_ = upstream_hosts_per_locality;
  init(true);
  updateHosts(local_hosts, local_hosts_per_locality);

  // Local cluster is not OK, we'll do regular routing.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(0U, stats_.lb_healthy_panic_.value());
  EXPECT_EQ(1U, stats_.lb_local_cluster_not_ok_.value());
}

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest,
       NoZoneAwareRoutingLocalEmptyFailTrafficOnPanic) {
  common_config_.mutable_zone_aware_lb_config()->set_fail_traffic_on_panic(true);

  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }

  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");

  HostVectorSharedPtr upstream_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b)}));
  HostVectorSharedPtr local_hosts(new HostVector({}, {}));

  HostsPerLocalitySharedPtr upstream_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b)}});
  HostsPerLocalitySharedPtr local_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:0", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:1", simTime(), zone_b)}});

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillOnce(Return(50))
      .WillOnce(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillOnce(Return(1));

  hostSet().healthy_hosts_ = *upstream_hosts;
  hostSet().hosts_ = *upstream_hosts;
  hostSet().healthy_hosts_per_locality_ = upstream_hosts_per_locality;
  init(true);
  updateHosts(local_hosts, local_hosts_per_locality);

  // Local cluster is not OK, we'll do regular routing (and select no host,
  // since we're in global panic).
  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));
  EXPECT_EQ(0U, stats_.lb_healthy_panic_.value());
  EXPECT_EQ(1U, stats_.lb_local_cluster_not_ok_.value());
}

// Validate that if we have healthy host lists >= 2, but there is no local
// locality included, that we skip zone aware routing and fallback.
TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, NoZoneAwareRoutingNoLocalLocality) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  HostVectorSharedPtr upstream_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b)}));
  HostVectorSharedPtr local_hosts(new HostVector());

  HostsPerLocalitySharedPtr upstream_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), zone_a)},
                            {makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), zone_b)}},
                           true);
  const HostsPerLocalitySharedPtr& local_hosts_per_locality = upstream_hosts_per_locality;

  hostSet().healthy_hosts_ = *upstream_hosts;
  hostSet().hosts_ = *upstream_hosts;
  hostSet().healthy_hosts_per_locality_ = upstream_hosts_per_locality;
  init(true);
  updateHosts(local_hosts, local_hosts_per_locality);

  // Local cluster is not OK, we'll do regular routing.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(0U, stats_.lb_healthy_panic_.value());
  EXPECT_EQ(1U, stats_.lb_local_cluster_not_ok_.value());
}

//////////////////////////////////////////////////////
// These tests verify ClientSideWeightedRoundRobinLoadBalancer specific functionality.
//

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest,
       updateWeightsOnHostsAllHostsHaveClientSideWeights) {
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

TEST_P(ClientSideWeightedRoundRobinLoadBalancerTest, DISABLED_chooseHostWithClientSideWeights) {
  hostSet().healthy_hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
      makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
      makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
  };
  hostSet().hosts_ = hostSet().healthy_hosts_;
  init(false);
  // DeterminePriorityLoad is called in chooseHost.
  ON_CALL(lb_context_, determinePriorityLoad(_, _, _)).WillByDefault(testing::ReturnArg<1>());

  lb_->addClientSideLbPolicyDataToHosts(hostSet().hosts_);
  simTime().setMonotonicTime(MonotonicTime(std::chrono::seconds(5)));
  for (const auto& host_ptr : hostSet().hosts_) {
    // chooseHost calls setOrcaLoadReportCallbacks.
    LoadBalancerContext::OrcaLoadReportCallbacks* orca_load_report_callbacks;
    EXPECT_CALL(lb_context_, setOrcaLoadReportCallbacks(_))
        .WillOnce(Invoke([&](LoadBalancerContext::OrcaLoadReportCallbacks& callbacks) {
          orca_load_report_callbacks = &callbacks;
        }));
    HostConstSharedPtr host = lb_->chooseHost(&lb_context_);
    // Hosts have equal weights, so chooseHost returns the current host.
    EXPECT_EQ(host, host_ptr);
    // Invoke the callback with an Orca load report.
    xds::data::orca::v3::OrcaLoadReport orca_load_report;
    orca_load_report.set_rps_fractional(1000);
    orca_load_report.set_application_utilization(0.5);
    // Orca load report callback does NOT change the host weight.
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
  absl::MutexLock lock(&client_side_data->mu_);
  // First report, so non_empty_since_ is updated.
  EXPECT_EQ(client_side_data->non_empty_since_, MonotonicTime(std::chrono::seconds(30)));
  // last_update_time_ is updated.
  EXPECT_EQ(client_side_data->last_update_time_, MonotonicTime(std::chrono::seconds(30)));
  // weight_ is calculated based on the Orca report.
  EXPECT_EQ(client_side_data->weight_, 2000);
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
  absl::MutexLock lock(&client_side_data->mu_);
  // non_empty_since_ is not updated.
  EXPECT_EQ(client_side_data->non_empty_since_, MonotonicTime(std::chrono::seconds(1)));
  // last_update_time_ is updated.
  EXPECT_EQ(client_side_data->last_update_time_, MonotonicTime(std::chrono::seconds(30)));
  // weight_ is recalculated based on the Orca report.
  EXPECT_EQ(client_side_data->weight_, 2000);
}

INSTANTIATE_TEST_SUITE_P(PrimaryOrFailoverAndLegacyOrNew,
                         ClientSideWeightedRoundRobinLoadBalancerTest,
                         ::testing::Values(LoadBalancerTestParam{true},
                                           LoadBalancerTestParam{false}));

// Unit tests for ClientSideWeightedRoundRobinLoadBalancer implementation.

TEST(ClientSideWeightedRoundRobinLoadBalancerTest,
     getClientSideWeightIfValidFromHost_NoClientSideData) {
  NiceMock<Envoy::Upstream::MockHost> host;
  EXPECT_FALSE(ClientSideWeightedRoundRobinLoadBalancerFriend::getClientSideWeightIfValidFromHost(
      host, MonotonicTime::min(), MonotonicTime::max()));
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, getClientSideWeightIfValidFromHost_TooRecent) {
  NiceMock<Envoy::Upstream::MockHost> host;
  auto client_side_data =
      std::make_shared<ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData>(
          42, /*non_empty_since=*/MonotonicTime(std::chrono::seconds(5)),
          /*last_update_time=*/MonotonicTime(std::chrono::seconds(10)));
  host.lb_policy_data_ = client_side_data;
  // Non empty since is too recent (5 > 2).
  EXPECT_FALSE(ClientSideWeightedRoundRobinLoadBalancerFriend::getClientSideWeightIfValidFromHost(
      host,
      /*min_non_empty_since=*/MonotonicTime(std::chrono::seconds(2)),
      /*max_last_update_time=*/MonotonicTime(std::chrono::seconds(8))));
  // non_empty_since_ is not updated.
  absl::MutexLock lock(&client_side_data->mu_);
  EXPECT_EQ(client_side_data->non_empty_since_, MonotonicTime(std::chrono::seconds(5)));
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, getClientSideWeightIfValidFromHost_TooStale) {
  NiceMock<Envoy::Upstream::MockHost> host;
  auto client_side_data =
      std::make_shared<ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData>(
          42, /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
          /*last_update_time=*/MonotonicTime(std::chrono::seconds(7)));
  host.lb_policy_data_ = client_side_data;
  // Last update time is too stale (7 < 8).
  EXPECT_FALSE(ClientSideWeightedRoundRobinLoadBalancerFriend::getClientSideWeightIfValidFromHost(
      host,
      /*min_non_empty_since=*/MonotonicTime(std::chrono::seconds(2)),
      /*max_last_update_time=*/MonotonicTime(std::chrono::seconds(8))));
  // Also resets the non_empty_since_ time.
  absl::MutexLock lock(&client_side_data->mu_);
  EXPECT_EQ(
      client_side_data->non_empty_since_,
      ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData::kDefaultNonEmptySince);
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, getClientSideWeightIfValidFromHost_Valid) {
  NiceMock<Envoy::Upstream::MockHost> host;
  auto client_side_data =
      std::make_shared<ClientSideWeightedRoundRobinLoadBalancer::ClientSideHostLbPolicyData>(
          42, /*non_empty_since=*/MonotonicTime(std::chrono::seconds(1)),
          /*last_update_time=*/MonotonicTime(std::chrono::seconds(10)));
  host.lb_policy_data_ = client_side_data;
  // Not empty since is not too recent (1 < 2) and last update time is not too
  // old (10 > 8).
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::getClientSideWeightIfValidFromHost(
                host,
                /*min_non_empty_since=*/MonotonicTime(std::chrono::seconds(2)),
                /*max_last_update_time=*/MonotonicTime(std::chrono::seconds(8)))
                .value(),
            42);
  // non_empty_since_ is not updated.
  absl::MutexLock lock(&client_side_data->mu_);
  EXPECT_EQ(client_side_data->non_empty_since_, MonotonicTime(std::chrono::seconds(1)));
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest,
     getUtilizationFromOrcaReport_applicationUtilization) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.set_application_utilization(0.5);
  orca_load_report.mutable_named_metrics()->insert({"foo", 0.3});
  orca_load_report.set_cpu_utilization(0.6);
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::getUtilizationFromOrcaReport(
                orca_load_report, {"foo"}),
            0.5);
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, getUtilizationFromOrcaReport_namedMetrics) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.mutable_named_metrics()->insert({"foo", 0.3});
  orca_load_report.set_cpu_utilization(0.6);
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::getUtilizationFromOrcaReport(
                orca_load_report, {"foo"}),
            0.3);
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, getUtilizationFromOrcaReport_cpuUtilization) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.mutable_named_metrics()->insert({"bar", 0.3});
  orca_load_report.set_cpu_utilization(0.6);
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::getUtilizationFromOrcaReport(
                orca_load_report, {"foo"}),
            0.6);
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, getUtilizationFromOrcaReport_noUtilization) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::getUtilizationFromOrcaReport(
                orca_load_report, {"foo"}),
            0);
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, calculateWeightFromOrcaReport_noQps) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::calculateWeightFromOrcaReport(
                orca_load_report, {"foo"}, 0.0)
                .status(),
            absl::InvalidArgumentError("QPS must be positive"));
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, calculateWeightFromOrcaReport_noUtilization) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.set_rps_fractional(1000);
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::calculateWeightFromOrcaReport(
                orca_load_report, {"foo"}, 0.0)
                .status(),
            absl::InvalidArgumentError("Utilization must be positive"));
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest,
     calculateWeightFromOrcaReport_ValidQpsAndUtilization) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.set_rps_fractional(1000);
  orca_load_report.set_application_utilization(0.5);
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::calculateWeightFromOrcaReport(
                orca_load_report, {"foo"}, 0.0)
                .value(),
            2000);
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest, calculateWeightFromOrcaReport_MaxWeight) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  // High QPS and low utilization.
  orca_load_report.set_rps_fractional(10000000000000L);
  orca_load_report.set_application_utilization(0.0000001);
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::calculateWeightFromOrcaReport(
                orca_load_report, {"foo"}, 0.0)
                .value(),
            /*std::numeric_limits<uint32_t>::max() = */ 4294967295);
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest,
     calculateWeightFromOrcaReport_ValidNoErrorPenalty) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.set_rps_fractional(1000);
  orca_load_report.set_eps(100);
  orca_load_report.set_application_utilization(0.5);
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::calculateWeightFromOrcaReport(
                orca_load_report, {"foo"}, 0.0)
                .value(),
            2000);
}

TEST(ClientSideWeightedRoundRobinLoadBalancerTest,
     calculateWeightFromOrcaReport_ValidWithErrorPenalty) {
  xds::data::orca::v3::OrcaLoadReport orca_load_report;
  orca_load_report.set_rps_fractional(1000);
  orca_load_report.set_eps(100);
  orca_load_report.set_application_utilization(0.5);
  EXPECT_EQ(ClientSideWeightedRoundRobinLoadBalancerFriend::calculateWeightFromOrcaReport(
                orca_load_report, {"foo"}, 2.0)
                .value(),
            1428);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
