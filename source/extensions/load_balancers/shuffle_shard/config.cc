#include "extensions/load_balancers/shuffle_shard/config.h"
#include "extensions/load_balancers/shuffle_shard/load_balancer.h"

#include "envoy/upstream/load_balancer.h"

#include "envoy/extensions/load_balancers/shuffle_shard/v3/shuffle_shard.pb.h"
#include "envoy/extensions/load_balancers/shuffle_shard/v3/shuffle_shard.pb.validate.h"

#include "envoy/config/metrics/v3/stats.pb.h"
#include "envoy/config/metrics/v3/stats.pb.validate.h"

#include "envoy/upstream/upstream.h"
#include "common/upstream/load_balancer_impl.h"


namespace Envoy {
namespace Extensions {
namespace LoadBalancer {
namespace ShuffleShard {

std::string LoadBalancerFactory::name() const { return "envoy.load_balancers.shuffle_shard"; }


Upstream::LoadBalancerPtr LoadBalancerFactory::createLoadBalancerWithConfig(Upstream::LoadBalancerType lb_type, const Upstream::PrioritySet& priority_set,
     const Upstream::PrioritySet* local_priority_set, Upstream::ClusterStats& stats,
     Runtime::Loader& runtime, Random::RandomGenerator& random, const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config,
     const envoy::extensions::load_balancers::shuffle_shard::v3::ShuffleShardConfig& config) {
       return std::make_unique<ShuffleShardLoadBalancer>(lb_type, priority_set, local_priority_set, stats, runtime, random, common_config, config);
}


// /**
//  * Static registration for the wasm factory. @see RegistryFactory.
//  */
REGISTER_FACTORY(LoadBalancerFactory, Upstream::TypedLoadBalancerFactory);

} // namespace ShuffleShard
} // namespace LoadBalancer
} // namespace Extensions
} // namespace Envoy
