#pragma once

#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/common/random_generator.h"
#include "source/common/memory/stats.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/simulated_time_system.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace Common {

class BaseTester : public Event::TestUsingSimulatedTime {
public:
  static constexpr absl::string_view metadata_key = "key";
  // We weight the first weighted_subset_percent of hosts with weight.
  BaseTester(uint64_t num_hosts, uint32_t weighted_subset_percent = 0, uint32_t weight = 0,
             bool attach_metadata = false);

  Envoy::Thread::MutexBasicLockable lock_;
  // Reduce default log level to warn while running this benchmark to avoid problems due to
  // excessive debug logging in upstream_impl.cc
  Envoy::Logger::Context logging_context_{spdlog::level::warn,
                                          Envoy::Logger::Logger::DEFAULT_LOG_FORMAT, lock_, false};

  Upstream::PrioritySetImpl priority_set_;
  Upstream::PrioritySetImpl local_priority_set_;

  // The following are needed to create a load balancer by the load balancer factory.
  Upstream::LoadBalancerParams lb_params_{priority_set_, &local_priority_set_};

  Stats::IsolatedStoreImpl stats_store_;
  Stats::Scope& stats_scope_{*stats_store_.rootScope()};
  Upstream::ClusterLbStatNames stat_names_{stats_store_.symbolTable()};
  Upstream::ClusterLbStats stats_{stat_names_, stats_scope_};
  NiceMock<Runtime::MockLoader> runtime_;
  Random::RandomGeneratorImpl random_;
  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
  envoy::config::cluster::v3::Cluster::RoundRobinLbConfig round_robin_lb_config_;
  std::shared_ptr<Upstream::MockClusterInfo> info_{new NiceMock<Upstream::MockClusterInfo>()};
};

} // namespace Common
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
