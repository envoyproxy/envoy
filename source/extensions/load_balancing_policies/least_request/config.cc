#include "source/extensions/load_balancing_policies/least_request/config.h"

#include "source/common/upstream/load_balancer_impl.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace LeastRequest {

Upstream::LoadBalancerPtr LeastRequestCreator::operator()(Upstream::LoadBalancerParams params,
                                                          const Upstream::ClusterInfo& cluster_info,
                                                          const Upstream::PrioritySet&,
                                                          Runtime::Loader& runtime,
                                                          Random::RandomGenerator& random,
                                                          TimeSource& time_source) {
  // Use the old proto type for now.
  envoy::config::cluster::v3::Cluster::LeastRequestLbConfig old_type_config;
  try {
    MessageUtil::wireCast(*cluster_info.loadBalancingPolicy(), old_type_config);
  } catch (EnvoyException& e) {
    // Cast failed and exit in debug mode. Continue in release mode with a error message.
    ASSERT(false,
           fmt::format("Failed to cast load balancing policy to least request: {}", e.what()));
    ENVOY_LOG(error,
              "Cannot cast least request load balancing policy configuration to "
              "envoy::config::cluster::v3::Cluster::LeastRequestLbConfig: {}",
              e.what());
    old_type_config.Clear();
  }
  return std::make_unique<Upstream::LeastRequestLoadBalancer>(
      params.priority_set, params.local_priority_set, cluster_info.stats(), runtime, random,
      cluster_info.lbConfig(), old_type_config, time_source);
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace LeastRequest
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
