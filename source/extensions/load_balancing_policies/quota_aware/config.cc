#include "source/extensions/load_balancing_policies/quota_aware/config.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/load_balancing_policies/quota_aware/v3/quota_aware.pb.h"
#include "envoy/extensions/load_balancing_policies/quota_aware/v3/quota_aware.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/load_balancing_policies/quota_aware/load_balancer.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace QuotaAware {

using QuotaAwareProto = envoy::extensions::load_balancing_policies::quota_aware::v3::QuotaAware;

absl::StatusOr<Upstream::LoadBalancerConfigPtr>
QuotaAwareLoadBalancerFactory::loadConfig(Server::Configuration::ServerFactoryContext& context,
                                          const Protobuf::Message& config) {
  const QuotaAwareProto& quota_aware_config =
      MessageUtil::downcastAndValidate<const QuotaAwareProto&>(config,
                                                               context.messageValidationVisitor());
  ASSERT(quota_aware_config.has_fallback_policy());
  return QuotaAwareLbConfig::make(quota_aware_config, context);
}

Upstream::ThreadAwareLoadBalancerPtr QuotaAwareLoadBalancerFactory::create(
    OptRef<const Upstream::LoadBalancerConfig> lb_config, const ClusterInfo& cluster_info,
    const PrioritySet& priority_set, Loader& runtime, RandomGenerator& random,
    TimeSource& time_source) {
  ASSERT(lb_config.has_value());
  const auto& quota_aware_lb_config = dynamic_cast<const QuotaAwareLbConfig&>(lb_config.ref());
  Upstream::ThreadAwareLoadBalancerPtr fallback_lb =
      quota_aware_lb_config.create(cluster_info, priority_set, runtime, random, time_source);
  ASSERT(fallback_lb != nullptr);
  return std::make_unique<QuotaAwareLoadBalancer>(quota_aware_lb_config, std::move(fallback_lb),
                                                 cluster_info.statsScope());
}

REGISTER_FACTORY(QuotaAwareLoadBalancerFactory, Upstream::TypedLoadBalancerFactory);

} // namespace QuotaAware
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
