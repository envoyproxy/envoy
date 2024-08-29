#pragma once

#include <memory>

#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.pb.h"
#include "envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/upstream/load_balancer_factory_base.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"
#include "source/extensions/load_balancing_policies/ring_hash/ring_hash_lb.h"

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

  Upstream::LoadBalancerConfigPtr loadConfig(Upstream::LoadBalancerFactoryContext&,
                                             const Protobuf::Message& config,
                                             ProtobufMessage::ValidationVisitor&) override {
    auto active_or_legacy =
        Common::ActiveOrLegacy<Upstream::RingHashLbProto, Upstream::ClusterProto>::get(&config);

    ASSERT(active_or_legacy.hasLegacy() || active_or_legacy.hasActive());

    return active_or_legacy.hasLegacy()
               ? Upstream::LoadBalancerConfigPtr{new Upstream::LegacyRingHashLbConfig(
                     *active_or_legacy.legacy())}
               : Upstream::LoadBalancerConfigPtr{
                     new Upstream::TypedRingHashLbConfig(*active_or_legacy.active())};
  }
};

} // namespace RingHash
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
