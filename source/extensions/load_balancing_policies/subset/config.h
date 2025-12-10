#pragma once

#include <memory>

#include "envoy/upstream/load_balancer.h"

#include "source/common/upstream/load_balancer_factory_base.h"
#include "source/extensions/load_balancing_policies/subset/subset_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace Subset {

class SubsetLbFactory
    : public Upstream::TypedLoadBalancerFactoryBase<Upstream::SubsetLbConfigProto> {
public:
  SubsetLbFactory() : TypedLoadBalancerFactoryBase("envoy.load_balancing_policies.subset") {}

  Upstream::ThreadAwareLoadBalancerPtr create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                              const Upstream::ClusterInfo& cluster_info,
                                              const Upstream::PrioritySet& priority_set,
                                              Runtime::Loader& runtime,
                                              Random::RandomGenerator& random,
                                              TimeSource& time_source) override;

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext& factory_context,
             const Protobuf::Message& config) override;

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadLegacy(Server::Configuration::ServerFactoryContext& factory_context,
             const Upstream::ClusterProto& cluster) override;
};

} // namespace Subset
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
