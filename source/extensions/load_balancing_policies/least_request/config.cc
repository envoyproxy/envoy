#include "source/extensions/load_balancing_policies/least_request/config.h"

#include "envoy/extensions/load_balancing_policies/least_request/v3/least_request.pb.h"

#include "source/common/upstream/load_balancer_impl.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace LeastRequest {

Upstream::LoadBalancerPtr LeastRequestCreator::operator()(
    Upstream::LoadBalancerParams params, OptRef<const Upstream::LoadBalancerConfig> lb_config,
    const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet&,
    Runtime::Loader& runtime, Random::RandomGenerator& random, TimeSource& time_source) {

  const auto typed_lb_confg =
      dynamic_cast<const Upstream::TypedLeastRequestLbConfig*>(lb_config.ptr());

  const auto legacy_lb_config =
      dynamic_cast<const Upstream::LegacyTypedLeastRequestLbConfig*>(lb_config.ptr());

  // The load balancing policy configuration will be loaded and validated in the main thread when we
  // load the cluster configuration. So we can assume the configuration is valid here.
  ASSERT(typed_lb_confg != nullptr || legacy_lb_config != nullptr,
         "Invalid load balancing policy configuration for least request load balancer");

  if (typed_lb_confg != nullptr) {
    return std::make_unique<Upstream::LeastRequestLoadBalancer>(
        params.priority_set, params.local_priority_set, cluster_info.lbStats(), runtime, random,
        PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(cluster_info.lbConfig(),
                                                       healthy_panic_threshold, 100, 50),
        typed_lb_confg->lb_config_, time_source);
  } else {
    return std::make_unique<Upstream::LeastRequestLoadBalancer>(
        params.priority_set, params.local_priority_set, cluster_info.lbStats(), runtime, random,
        cluster_info.lbConfig(),
        legacy_lb_config->lb_config_.has_value()
            ? legacy_lb_config->lb_config_.value()
            : OptRef<const envoy::config::cluster::v3::Cluster::LeastRequestLbConfig>{},
        time_source);
  }
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace LeastRequest
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
