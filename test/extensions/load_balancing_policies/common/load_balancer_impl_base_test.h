#pragma once

#include <bitset>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"

#include "source/common/common/random_generator.h"
#include "source/common/network/utility.h"
#include "source/common/upstream/load_balancer_context_base.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Upstream {

class EdfLoadBalancerBasePeer {
public:
  static const std::chrono::milliseconds& slowStartWindow(EdfLoadBalancerBase& edf_lb) {
    return edf_lb.slow_start_window_;
  }
  static const std::chrono::milliseconds latestHostAddedTime(EdfLoadBalancerBase& edf_lb) {
    return std::chrono::time_point_cast<std::chrono::milliseconds>(edf_lb.latest_host_added_time_)
        .time_since_epoch();
  }
  static double slowStartMinWeightPercent(const EdfLoadBalancerBase& edf_lb) {
    return edf_lb.slow_start_min_weight_percent_;
  }
};

class TestZoneAwareLoadBalancer : public ZoneAwareLoadBalancerBase {
public:
  TestZoneAwareLoadBalancer(
      const PrioritySet& priority_set, ClusterLbStats& lb_stats, Runtime::Loader& runtime,
      Random::RandomGenerator& random,
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
      : ZoneAwareLoadBalancerBase(
            priority_set, nullptr, lb_stats, runtime, random,
            PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(common_config, healthy_panic_threshold,
                                                           100, 50),
            LoadBalancerConfigHelper::localityLbConfigFromCommonLbConfig(common_config)) {}
  void runInvalidLocalitySourceType() {
    localitySourceType(static_cast<LoadBalancerBase::HostAvailability>(123));
  }
  void runInvalidSourceType() { sourceType(static_cast<LoadBalancerBase::HostAvailability>(123)); }
  HostConstSharedPtr chooseHostOnce(LoadBalancerContext*) override { PANIC("not implemented"); }
  HostConstSharedPtr peekAnotherHost(LoadBalancerContext*) override { PANIC("not implemented"); }
};

struct LoadBalancerTestParam {
  bool use_default_host_set;
};

class LoadBalancerTestBase : public Event::TestUsingSimulatedTime,
                             public testing::TestWithParam<LoadBalancerTestParam> {
protected:
  // Run all tests against both priority 0 and priority 1 host sets, to ensure
  // all the load balancers have equivalent functionality for failover host sets.
  MockHostSet& hostSet() {
    return GetParam().use_default_host_set ? host_set_ : failover_host_set_;
  }

  LoadBalancerTestBase()
      : stat_names_(stats_store_.symbolTable()), stats_(stat_names_, *stats_store_.rootScope()) {
    least_request_lb_config_.mutable_choice_count()->set_value(2);
  }

  Stats::IsolatedStoreImpl stats_store_;
  ClusterLbStatNames stat_names_;
  ClusterLbStats stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<MockPrioritySet> priority_set_;
  MockHostSet& host_set_ = *priority_set_.getMockHostSet(0);
  MockHostSet& failover_host_set_ = *priority_set_.getMockHostSet(1);
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
  envoy::config::cluster::v3::Cluster::LeastRequestLbConfig least_request_lb_config_;
  envoy::config::cluster::v3::Cluster::RoundRobinLbConfig round_robin_lb_config_;
};

} // namespace Upstream
} // namespace Envoy
