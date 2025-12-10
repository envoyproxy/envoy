#include "source/common/network/address_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/priority_set.h"

#include "contrib/peak_ewma/load_balancing_policies/source/peak_ewma_lb.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PeakEwma {

// Peak EWMA integration tests using simple LoadBalancerBase pattern

class PeakEwmaLoadBalancerIntegrationTest : public ::testing::Test {
public:
  PeakEwmaLoadBalancerIntegrationTest()
      : stat_names_(store_.symbolTable()), stats_(stat_names_, *store_.rootScope()) {

    // Create 3 real host implementations to avoid mock complexity
    for (int i = 0; i < 3; ++i) {
      auto address =
          std::make_shared<Network::Address::Ipv4Instance>("10.0.0." + std::to_string(i + 1), 0);
      auto host = std::make_shared<testing::NiceMock<Upstream::MockHost>>();
      ON_CALL(*host, address()).WillByDefault(Return(address));
      hosts_.emplace_back(host);
    }
  }

  void SetUp() override {
    host_set_ = priority_set_.getMockHostSet(0);
    host_set_->hosts_ = hosts_;
    host_set_->healthy_hosts_ = hosts_;

    ON_CALL(priority_set_, hostSetsPerPriority())
        .WillByDefault(ReturnRef(priority_set_.host_sets_));
    ON_CALL(*cluster_info_, statsScope()).WillByDefault(ReturnRef(*store_.rootScope()));
    ON_CALL(time_source_, monotonicTime())
        .WillByDefault(Return(MonotonicTime(std::chrono::milliseconds(1234567890))));

    envoy::extensions::load_balancing_policies::peak_ewma::v3alpha::PeakEwma config;
    config.mutable_decay_time()->set_seconds(10);

    lb_ = std::make_unique<PeakEwmaLoadBalancer>(priority_set_, nullptr, stats_, runtime_, random_,
                                                 50, *cluster_info_, time_source_, config,
                                                 dispatcher_);
  }

protected:
  Stats::TestUtil::TestStore store_;
  Upstream::ClusterLbStatNames stat_names_;
  Upstream::ClusterLbStats stats_;
  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info_{
      std::make_shared<NiceMock<Upstream::MockClusterInfo>>()};
  NiceMock<Upstream::MockPrioritySet> priority_set_;
  Upstream::MockHostSet* host_set_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<MockTimeSystem> time_source_;
  NiceMock<Event::MockDispatcher> dispatcher_;

  std::vector<Upstream::HostSharedPtr> hosts_;
  std::unique_ptr<PeakEwmaLoadBalancer> lb_;
};

// Test basic integration - load balancer chooses a host
TEST_F(PeakEwmaLoadBalancerIntegrationTest, ChoosesHost) {
  auto result = lb_->chooseHost(nullptr);
  EXPECT_NE(result.host, nullptr);
}

// Test single host scenario
TEST_F(PeakEwmaLoadBalancerIntegrationTest, SingleHost) {
  host_set_->hosts_ = {hosts_[0]};
  host_set_->healthy_hosts_ = {hosts_[0]};

  auto result = lb_->chooseHost(nullptr);
  EXPECT_EQ(result.host, hosts_[0]);
}

// Test no hosts scenario
TEST_F(PeakEwmaLoadBalancerIntegrationTest, NoHosts) {
  host_set_->hosts_ = {};
  host_set_->healthy_hosts_ = {};

  auto result = lb_->chooseHost(nullptr);
  EXPECT_EQ(result.host, nullptr);
}

// Test interface implementation
TEST_F(PeakEwmaLoadBalancerIntegrationTest, PeekAnotherHost) {
  auto result = lb_->peekAnotherHost(nullptr);
  EXPECT_EQ(result, nullptr); // Peak EWMA doesn't support peeking
}

} // namespace PeakEwma
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
