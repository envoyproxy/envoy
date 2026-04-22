#pragma once

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
    Upstream::TypedLoadBalancerFactory* endpoint_picking_policy_factory = nullptr;
    // Iterate through the list of endpoint picking policies to find the first one that we know
    // about.
    for (const auto& endpoint_picking_policy : lb_config.endpoint_picking_policy().policies()) {
      endpoint_picking_policy_factory =
          Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(
              endpoint_picking_policy.typed_extension_config(),
              /*is_optional=*/true);

      if (endpoint_picking_policy_factory != nullptr) {
        // Ensure that the endpoint picking policy is a ClientSideWeightedRoundRobin.
        auto* client_side_weighted_round_robin_factory = dynamic_cast<
            ::Envoy::Extensions::LoadBalancingPolicies::ClientSideWeightedRoundRobin::Factory*>(
            endpoint_picking_policy_factory);
        if (client_side_weighted_round_robin_factory == nullptr) {
          return absl::InvalidArgumentError(
              "Currently WrrLocalityLoadBalancer only supports "
              "ClientSideWeightedRoundRobinLoadBalancer as its endpoint "
              "picking policy.");
        }
        // Load and validate the configuration.
        auto sub_lb_proto_message = endpoint_picking_policy_factory->createEmptyConfigProto();
        RETURN_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
            endpoint_picking_policy.typed_extension_config().typed_config(),
            context.messageValidationVisitor(), *sub_lb_proto_message));

        auto lb_config_or_error =
            endpoint_picking_policy_factory->loadConfig(context, *sub_lb_proto_message);
        RETURN_IF_NOT_OK(lb_config_or_error.status());

        auto wrr_locality_lb_config = std::make_unique<WrrLocalityLbConfig>(
            *endpoint_picking_policy_factory, std::move(lb_config_or_error.value()));
        return Upstream::LoadBalancerConfigPtr{wrr_locality_lb_config.release()};
      }
    }

    return absl::InvalidArgumentError("No supported endpoint picking policy.");
  }
};

DECLARE_FACTORY(Factory);

} // namespace WrrLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
