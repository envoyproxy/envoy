#pragma once

#include "envoy/extensions/load_balancing_policies/load_aware_locality/v3/load_aware_locality.pb.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/upstream/load_balancer_factory_base.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace LoadAwareLocality {

using LoadAwareLocalityProto =
    envoy::extensions::load_balancing_policies::load_aware_locality::v3::LoadAwareLocality;

class Factory : public Upstream::TypedLoadBalancerFactoryBase<LoadAwareLocalityProto> {
public:
  Factory() : TypedLoadBalancerFactoryBase("envoy.load_balancing_policies.load_aware_locality") {}

  Upstream::ThreadAwareLoadBalancerPtr create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                              const Upstream::ClusterInfo& cluster_info,
                                              const Upstream::PrioritySet& priority_set,
                                              Runtime::Loader& runtime,
                                              Envoy::Random::RandomGenerator& random,
                                              TimeSource& time_source) override;

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext& context,
             const Protobuf::Message& config) override;
};

DECLARE_FACTORY(Factory);

} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
