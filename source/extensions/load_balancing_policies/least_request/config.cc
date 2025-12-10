#include "source/extensions/load_balancing_policies/least_request/config.h"

#include "envoy/extensions/load_balancing_policies/least_request/v3/least_request.pb.h"

#include "source/extensions/load_balancing_policies/least_request/least_request_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace LeastRequest {

TypedLeastRequestLbConfig::TypedLeastRequestLbConfig(const CommonLbConfigProto& common_lb_config,
                                                     const LegacyLeastRequestLbProto& lb_config) {
  Upstream::LoadBalancerConfigHelper::convertLocalityLbConfigTo(common_lb_config, lb_config_);

  Upstream::LoadBalancerConfigHelper::convertSlowStartConfigTo(lb_config, lb_config_);
  if (lb_config.has_choice_count()) {
    *lb_config_.mutable_choice_count() = lb_config.choice_count();
  }
  if (lb_config.has_active_request_bias()) {
    *lb_config_.mutable_active_request_bias() = lb_config.active_request_bias();
  }
}

TypedLeastRequestLbConfig::TypedLeastRequestLbConfig(const LeastRequestLbProto& lb_config)
    : lb_config_(lb_config) {}

Upstream::LoadBalancerPtr LeastRequestCreator::operator()(
    Upstream::LoadBalancerParams params, OptRef<const Upstream::LoadBalancerConfig> lb_config,
    const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet&,
    Runtime::Loader& runtime, Random::RandomGenerator& random, TimeSource& time_source) {

  const auto typed_lb_config = dynamic_cast<const TypedLeastRequestLbConfig*>(lb_config.ptr());
  ASSERT(typed_lb_config != nullptr, "Invalid least request load balancer config");

  return std::make_unique<Upstream::LeastRequestLoadBalancer>(
      params.priority_set, params.local_priority_set, cluster_info.lbStats(), runtime, random,
      PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(cluster_info.lbConfig(),
                                                     healthy_panic_threshold, 100, 50),
      typed_lb_config->lb_config_, time_source);
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace LeastRequest
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
