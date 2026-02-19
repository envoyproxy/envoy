#pragma once

#include <memory>

#include "envoy/extensions/load_balancing_policies/rendezvous_hash/v3/rendezvous_hash.pb.h"
#include "envoy/extensions/load_balancing_policies/rendezvous_hash/v3/rendezvous_hash.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/upstream/load_balancer_factory_base.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"
#include "source/extensions/load_balancing_policies/rendezvous_hash/rendezvous_hash_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace RendezvousHash {

using RendezvousHashLbProto =
    envoy::extensions::load_balancing_policies::rendezvous_hash::v3::RendezvousHash;

class Factory : public Upstream::TypedLoadBalancerFactoryBase<RendezvousHashLbProto> {
public:
  Factory() : TypedLoadBalancerFactoryBase("envoy.load_balancing_policies.rendezvous_hash") {}

  Upstream::ThreadAwareLoadBalancerPtr create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                              const Upstream::ClusterInfo& cluster_info,
                                              const Upstream::PrioritySet& priority_set,
                                              Runtime::Loader& runtime,
                                              Random::RandomGenerator& random,
                                              TimeSource& time_source) override;

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext& context,
             const Protobuf::Message& config) override {
    ASSERT(dynamic_cast<const RendezvousHashLbProto*>(&config) != nullptr);
    const RendezvousHashLbProto& typed_proto = dynamic_cast<const RendezvousHashLbProto&>(config);
    absl::Status creation_status = absl::OkStatus();
    auto typed_config = std::make_unique<Upstream::TypedRendezvousHashLbConfig>(
        typed_proto, context.regexEngine(), creation_status);
    RETURN_IF_NOT_OK_REF(creation_status);
    return typed_config;
  }
};

} // namespace RendezvousHash
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
