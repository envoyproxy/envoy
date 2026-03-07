#include "source/extensions/load_balancing_policies/load_aware_locality/config.h"

#include "source/common/config/utility.h"
#include "source/extensions/load_balancing_policies/load_aware_locality/load_aware_locality_lb.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace LoadAwareLocality {

Upstream::ThreadAwareLoadBalancerPtr
Factory::create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                const Upstream::ClusterInfo& cluster_info,
                const Upstream::PrioritySet& priority_set, Runtime::Loader& runtime,
                Envoy::Random::RandomGenerator& random, TimeSource& time_source) {
  return std::make_unique<LoadAwareLocalityLoadBalancer>(lb_config, cluster_info, priority_set,
                                                         runtime, random, time_source);
}

absl::StatusOr<Upstream::LoadBalancerConfigPtr>
Factory::loadConfig(Server::Configuration::ServerFactoryContext& context,
                    const Protobuf::Message& config) {
  const auto& lb_config = dynamic_cast<const LoadAwareLocalityLbProto&>(config);

  // Resolve the endpoint-picking child policy.
  Upstream::TypedLoadBalancerFactory* endpoint_picking_policy_factory = nullptr;
  for (const auto& endpoint_picking_policy : lb_config.endpoint_picking_policy().policies()) {
    endpoint_picking_policy_factory =
        Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(
            endpoint_picking_policy.typed_extension_config(),
            /*is_optional=*/true);

    if (endpoint_picking_policy_factory != nullptr) {
      // Load and validate the child policy configuration.
      auto sub_lb_proto_message = endpoint_picking_policy_factory->createEmptyConfigProto();
      RETURN_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
          endpoint_picking_policy.typed_extension_config().typed_config(),
          context.messageValidationVisitor(), *sub_lb_proto_message));

      auto lb_config_or_error =
          endpoint_picking_policy_factory->loadConfig(context, *sub_lb_proto_message);
      RETURN_IF_NOT_OK(lb_config_or_error.status());

      auto weight_update_period = std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(lb_config, weight_update_period, 1000));
      if (weight_update_period.count() <= 0) {
        return absl::InvalidArgumentError(absl::StrCat(
            "weight_update_period must be > 0ms, got: ", weight_update_period.count(), "ms"));
      }

      const double utilization_variance_threshold =
          lb_config.has_utilization_variance_threshold()
              ? lb_config.utilization_variance_threshold().value()
              : 0.1;

      const double ewma_alpha = lb_config.has_ewma_alpha() ? lb_config.ewma_alpha().value() : 0.3;

      const double probe_percentage =
          lb_config.has_probe_percentage() ? lb_config.probe_percentage().value() : 0.03;

      auto weight_expiration_period = std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(lb_config, weight_expiration_period, 180000));

      return std::make_unique<LoadAwareLocalityLbConfig>(
          *endpoint_picking_policy_factory,
          LoadBalancerConfigSharedPtr(std::move(lb_config_or_error.value())), weight_update_period,
          utilization_variance_threshold, ewma_alpha, probe_percentage, weight_expiration_period,
          context.mainThreadDispatcher(), context.threadLocal());
    }
  }

  return absl::InvalidArgumentError("No supported endpoint picking policy.");
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(Factory, Upstream::TypedLoadBalancerFactory);

} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
