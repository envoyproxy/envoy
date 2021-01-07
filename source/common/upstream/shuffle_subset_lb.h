#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/load_balancer.h"

#include "common/common/macros.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/upstream/upstream_impl.h"

#include "absl/container/node_hash_map.h"
#include "absl/types/optional.h"
#include "quiche/quic/core/quic_lru_cache.h"

namespace Envoy {
namespace Upstream {

class ShuffleSubsetLoadBalancer : public LoadBalancer, Logger::Loggable<Logger::Id::upstream> {
public:
  ShuffleSubsetLoadBalancer(
      LoadBalancerType lb_type, PrioritySet& priority_set, const PrioritySet* local_priority_set,
      ClusterStats& stats, Stats::Scope& scope, Runtime::Loader& runtime,
      Random::RandomGenerator& random, const LoadBalancerShuffleSubsetInfo& subsets,
      const absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig>&
          lb_ring_hash_config,
      const absl::optional<envoy::config::cluster::v3::Cluster::MaglevLbConfig>& lb_maglev_config,
      const absl::optional<envoy::config::cluster::v3::Cluster::LeastRequestLbConfig>&
          least_request_config,
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config);
  ~ShuffleSubsetLoadBalancer() override;

  // Upstream::LoadBalancer
  HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;
  // TODO(alyssawilk) implement for non-metadata match.
  HostConstSharedPtr peekAnotherHost(LoadBalancerContext*) override { return nullptr; }

private:
  using HostPredicate = std::function<bool(const Host&)>;

  // Represents a subset of an original HostSet.
  class HostSubsetImpl : public HostSetImpl {
  public:
    HostSubsetImpl(const HostSet& original_host_set) :
      HostSetImpl(original_host_set.priority(), original_host_set.overprovisioningFactor()),
      original_host_set_(original_host_set) {}

    void update(const HostVector& hosts_added, const HostVector& hosts_removed,
                HostPredicate predicate);

  private:
    const HostSet& original_host_set_;
  };

  // Represents a subset of an original PrioritySet.
  class PriorityShuffleSubsetImpl : public PrioritySetImpl {
  public:
    PriorityShuffleSubsetImpl(const ShuffleSubsetLoadBalancer& subset_lb, std::vector<uint32_t> * set);//,

    void update(uint32_t priority, const HostVector& hosts_added, const HostVector& hosts_removed);

    void triggerCallbacks() {
      for (size_t i = 0; i < hostSetsPerPriority().size(); ++i) {
        runReferenceUpdateCallbacks(i, {}, {});
      }
    }
    void updateSubset(uint32_t priority, const HostVector& hosts_added,
                      const HostVector& hosts_removed);

    // Thread aware LB if applicable.
    ThreadAwareLoadBalancerPtr thread_aware_lb_;
    // Current active LB.
    LoadBalancerPtr lb_;

    std::vector<uint32_t> * set_;

  protected:
    HostSetImplPtr createHostSet(uint32_t priority,
                                 absl::optional<uint32_t> overprovisioning_factor) override;

  private:
    const PrioritySet& original_priority_set_;
    const HostPredicate predicate_;
  // private:
  };

  // using HostSubsetImplPtr = std::shared_ptr<HostSubsetImpl>;
  using PriorityShuffleSubsetImplPtr = std::shared_ptr<PriorityShuffleSubsetImpl>;

  std::vector<uint32_t> * combo(uint32_t i, uint32_t n, uint32_t k);

  void update(uint32_t priority, const HostVector& hosts_added, const HostVector& hosts_removed);

  const LoadBalancerType lb_type_;
  const absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig> lb_ring_hash_config_;
  const absl::optional<envoy::config::cluster::v3::Cluster::MaglevLbConfig> lb_maglev_config_;
  const absl::optional<envoy::config::cluster::v3::Cluster::LeastRequestLbConfig>
      least_request_config_;
  const envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
  ClusterStats& stats_;
  Stats::Scope& scope_;
  Runtime::Loader& runtime_;
  Random::RandomGenerator& random_;

  const uint32_t shard_size_;
  const uint32_t cache_capacity_;

  const PrioritySet& original_priority_set_;
  const PrioritySet* original_local_priority_set_;
  Common::CallbackHandle* original_priority_set_callback_handle_;

  quic::QuicLinkedHashMap<uint32_t, PriorityShuffleSubsetImplPtr> cache_;
  uint32_t num_hosts_;
  uint32_t num_indices_;
  // PriorityShuffleSubsetImplPtr priority_subset_;

  friend class ShuffleSubsetLoadBalancerDescribeMetadataTester;
};

} // namespace Upstream
} // namespace Envoy
