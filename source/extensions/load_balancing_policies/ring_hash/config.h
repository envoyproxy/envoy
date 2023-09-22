#pragma once

#include <memory>

#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.pb.h"
#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/upstream/load_balancer_factory_base.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace RingHash {

using RingHashLbProto = envoy::extensions::load_balancing_policies::ring_hash::v3::RingHash;

class Factory : public Upstream::TypedLoadBalancerFactoryBase<RingHashLbProto> {
public:
  Factory() : TypedLoadBalancerFactoryBase("envoy.load_balancing_policies.ring_hash") {}

  Upstream::ThreadAwareLoadBalancerPtr create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                              const Upstream::ClusterInfo& cluster_info,
                                              const Upstream::PrioritySet& priority_set,
                                              Runtime::Loader& runtime,
                                              Random::RandomGenerator& random,
                                              TimeSource& time_source) override;
};

} // namespace RingHash
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
