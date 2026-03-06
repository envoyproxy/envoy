#pragma once

#include <memory>

#include "envoy/extensions/load_balancing_policies/load_aware_locality/v3/load_aware_locality.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"
#include "source/extensions/load_balancing_policies/load_aware_locality/load_aware_locality_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace LoadAwareLocality {

using LoadAwareLocalityLbProto =
    envoy::extensions::load_balancing_policies::load_aware_locality::v3::LoadAwareLocality;

class Factory : public Upstream::TypedLoadBalancerFactoryBase<LoadAwareLocalityLbProto> {
public:
  Factory()
      : Upstream::TypedLoadBalancerFactoryBase<LoadAwareLocalityLbProto>(
            "envoy.load_balancing_policies.load_aware_locality") {}

  Upstream::ThreadAwareLoadBalancerPtr create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                              const Upstream::ClusterInfo& cluster_info,
                                              const Upstream::PrioritySet& priority_set,
                                              Runtime::Loader& runtime,
                                              Envoy::Random::RandomGenerator& random,
                                              TimeSource& time_source) override;

  bool requiresOrcaLoadReports() const override { return true; }

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext& context,
             const Protobuf::Message& config) override;
};

DECLARE_FACTORY(Factory);

} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
