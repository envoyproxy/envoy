#include "infima/infima.h"

#pragma once

#include "envoy/extensions/load_balancers/shuffle_shard/v3/shuffle_shard.pb.h"
#include "envoy/extensions/load_balancers/shuffle_shard/v3/shuffle_shard.pb.validate.h"

#include "common/upstream/load_balancer_impl.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancer {
namespace ShuffleShard {

class ShuffleShardLoadBalancer : public Upstream::ZoneAwareLoadBalancerBase {
public:
  ShuffleShardLoadBalancer(Upstream::LoadBalancerType lb_type, const Upstream::PrioritySet& priority_set,
                           const Upstream::PrioritySet* local_priority_set, Upstream::ClusterStats& stats,
                           Runtime::Loader& runtime, Random::RandomGenerator& random,
                           const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config,
                           const envoy::extensions::load_balancers::shuffle_shard::v3::ShuffleShardConfig& config);

  Upstream::HostConstSharedPtr chooseHostOnce(Upstream::LoadBalancerContext* context) override;

  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override { return nullptr; }

private:
  void remove_hosts(const Upstream::HostVector&);

  void add_hosts(const Upstream::HostVector&);

  absl::optional<std::vector<std::string>> get_coord(const Upstream::HostConstSharedPtr&);

  // const Upstream::LoadBalancerType lb_type_;
  const uint32_t endpoints_per_cell_;
  const bool use_zone_as_dimension_;
  std::vector<std::string> dimensions_;
  const bool use_dimensions_;
  const uint32_t least_request_choice_count_;
  Lattice<Upstream::HostConstSharedPtr>* lattice_;
  ShuffleSharder<Upstream::HostConstSharedPtr> shuffle_sharder_;
  ::Envoy::Common::CallbackHandlePtr priority_update_cb_;
};

} // namespace ShuffleShard
} // namespace LoadBalancer
} // namespace Extensions
} // namespace Envoy
