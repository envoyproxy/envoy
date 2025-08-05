#pragma once

#ifndef NET_ENVOY_SOURCE_EXTENSIONS_LOAD_BALANCERS_WRR_LOCALITY_CONFIG_H_
#define NET_ENVOY_SOURCE_EXTENSIONS_LOAD_BALANCERS_WRR_LOCALITY_CONFIG_H_

#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3/client_side_weighted_round_robin.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/load_balancing_policies/client_side_weighted_round_robin/client_side_weighted_round_robin_lb.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"
#include "source/extensions/load_balancing_policies/wrr_locality/wrr_locality_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace WrrLocality {

class Factory : public TypedLoadBalancerFactoryBase<WrrLocalityLbProto> {
public:
  Factory()
      : TypedLoadBalancerFactoryBase<WrrLocalityLbProto>(
            "envoy.load_balancing_policies.wrr_locality") {}

  Upstream::ThreadAwareLoadBalancerPtr create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                              const Upstream::ClusterInfo& cluster_info,
                                              const Upstream::PrioritySet& priority_set,
                                              Runtime::Loader& runtime,
                                              Envoy::Random::RandomGenerator& random,
                                              TimeSource& time_source) override {
    return std::make_unique<WrrLocalityLoadBalancer>(lb_config, cluster_info, priority_set, runtime,
                                                     random, time_source);
  }

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext& context,
             const Protobuf::Message& config) override {
    const auto& lb_config = dynamic_cast<const WrrLocalityLbProto&>(config);
    auto wrr_locality_lb_config = std::make_unique<WrrLocalityLbConfig>(lb_config, context);
    // Ensure that the endpoint picking policy is a
    // ClientSideWeightedRoundRobin.
    auto* client_side_weighted_round_robin_factory = dynamic_cast<
        ::Envoy::Extensions::LoadBalancingPolices::ClientSideWeightedRoundRobin::Factory*>(
        wrr_locality_lb_config->endpoint_picking_policy_factory_);
    if (client_side_weighted_round_robin_factory == nullptr) {
      return absl::InvalidArgumentError("Currently WrrLocalityLoadBalancer only supports "
                                        "ClientSideWeightedRoundRobinLoadBalancer as its endpoint "
                                        "picking policy.");
    }
    return Upstream::LoadBalancerConfigPtr{wrr_locality_lb_config.release()};
  }
};

DECLARE_FACTORY(Factory);

} // namespace WrrLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy

#endif // NET_ENVOY_SOURCE_EXTENSIONS_LOAD_BALANCERS_WRR_LOCALITY_CONFIG_H_
