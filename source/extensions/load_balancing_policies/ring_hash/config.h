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
namespace LoadBalancingPolicies {
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

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext& context,
             const Protobuf::Message& config) override {
    ASSERT(dynamic_cast<const RingHashLbProto*>(&config) != nullptr);
    const RingHashLbProto& typed_proto = dynamic_cast<const RingHashLbProto&>(config);
    absl::Status creation_status = absl::OkStatus();
    auto typed_config = std::make_unique<Upstream::TypedRingHashLbConfig>(
        typed_proto, context.regexEngine(), creation_status);
    RETURN_IF_NOT_OK_REF(creation_status);
    return typed_config;
  }

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadLegacy(Server::Configuration::ServerFactoryContext&,
             const Upstream::ClusterProto& cluster) override {
    return std::make_unique<Upstream::TypedRingHashLbConfig>(cluster.common_lb_config(),
                                                             cluster.ring_hash_lb_config());
  }
};

} // namespace RingHash
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
