#pragma once

#include "envoy/common/pure.h"

#include "envoy/extensions/load_balancers/shuffle_shard/v3/shuffle_shard.pb.h"
#include "envoy/extensions/load_balancers/shuffle_shard/v3/shuffle_shard.pb.validate.h"

#include "common/upstream/load_balancer_impl.h"

#include "envoy/config/typed_config.h"

#include "envoy/registry/registry.h"
#include "envoy/config/cluster/v3/cluster.pb.h"


namespace Envoy {
namespace Extensions {
namespace LoadBalancer {
namespace ShuffleShard {

class LoadBalancerFactory : public Upstream::ConfigurableTypedLoadBalancerFactory<
                                envoy::extensions::load_balancers::shuffle_shard::v3::ShuffleShardConfig> {

public:
  std::string name() const override;
  Upstream::LoadBalancerPtr createLoadBalancerWithConfig(Upstream::LoadBalancerType, const Upstream::PrioritySet&, const Upstream::PrioritySet*, Upstream::ClusterStats&,
       Runtime::Loader&, Random::RandomGenerator&, const envoy::config::cluster::v3::Cluster::CommonLbConfig&,
       const envoy::extensions::load_balancers::shuffle_shard::v3::ShuffleShardConfig&) override;
};

DECLARE_FACTORY(LoadBalancerFactory);

} // namespace ShuffleShard
} // namespace LoadBalancer
} // namespace Extensions
} // namespace Envoy
