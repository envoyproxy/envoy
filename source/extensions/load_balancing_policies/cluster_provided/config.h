#pragma once

#include <memory>

#include "envoy/extensions/load_balancing_policies/cluster_provided/v3/cluster_provided.pb.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/upstream/load_balancer_factory_base.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace ClusterProvided {

class ClusterProvidedLbConfig : public Upstream::LoadBalancerConfig {
public:
  ClusterProvidedLbConfig() = default;
};

class Factory
    : public Upstream::TypedLoadBalancerFactoryBase<
          envoy::extensions::load_balancing_policies::cluster_provided::v3::ClusterProvided> {
public:
  Factory() : TypedLoadBalancerFactoryBase("envoy.load_balancing_policies.cluster_provided") {}

  Upstream::ThreadAwareLoadBalancerPtr create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                              const Upstream::ClusterInfo& cluster_info,
                                              const Upstream::PrioritySet& priority_set,
                                              Runtime::Loader& runtime,
                                              Random::RandomGenerator& random,
                                              TimeSource& time_source) override;

  Upstream::LoadBalancerConfigPtr loadConfig(const Protobuf::Message&,
                                             ProtobufMessage::ValidationVisitor&) override {
    return std::make_unique<ClusterProvidedLbConfig>();
  }
};

DECLARE_FACTORY(Factory);

} // namespace ClusterProvided
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
