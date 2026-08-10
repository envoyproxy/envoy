#include "source/extensions/load_balancing_policies/load_aware_locality/config.h"

#include <cmath>

#include "source/common/config/utility.h"
#include "source/extensions/load_balancing_policies/load_aware_locality/load_aware_locality_lb.h"

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
  const auto& lb_config = Envoy::Protobuf::DynamicCastMessage<LoadAwareLocalityLbProto>(config);

  // Validate policy-level knobs before resolving the child policy so a config error is reported
  // for what it is rather than masked by an unresolvable child.
  const std::chrono::milliseconds weight_update_period =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(lb_config, weight_update_period, 1000));
  // PGV constraints are not enforced on unpacked LB configs, so the documented floor is owned
  // here.
  if (weight_update_period < std::chrono::milliseconds(100)) {
    return absl::InvalidArgumentError("weight_update_period must be at least 100ms");
  }
  const std::chrono::milliseconds smoothing_time_constant = std::chrono::milliseconds(
      PROTOBUF_GET_MS_OR_DEFAULT(lb_config, smoothing_time_constant, 5000));
  if (smoothing_time_constant <= std::chrono::milliseconds(0)) {
    return absl::InvalidArgumentError("smoothing_time_constant must be positive");
  }
  // Derive the per-tick EWMA factor so settling time is independent of tick rate.
  const double ewma_alpha =
      1.0 - std::exp(-std::chrono::duration<double>(weight_update_period).count() /
                     std::chrono::duration<double>(smoothing_time_constant).count());
  const double utilization_variance_threshold =
      lb_config.has_utilization_variance_threshold()
          ? lb_config.utilization_variance_threshold().value()
          : 0.1;
  if (!(utilization_variance_threshold >= 0.0 && utilization_variance_threshold <= 1.0)) {
    return absl::InvalidArgumentError("utilization_variance_threshold must be in [0, 1]");
  }
  const double remote_probe_fraction =
      lb_config.has_remote_probe_fraction() ? lb_config.remote_probe_fraction().value() : 0.03;
  if (!(remote_probe_fraction >= 0.0 && remote_probe_fraction < 1.0)) {
    return absl::InvalidArgumentError("remote_probe_fraction must be in [0, 1)");
  }
  const std::chrono::milliseconds weight_expiration_period = std::chrono::milliseconds(
      PROTOBUF_GET_MS_OR_DEFAULT(lb_config, weight_expiration_period, 180000));
  std::vector<std::string> metric_names(lb_config.metric_names_for_computing_utilization().begin(),
                                        lb_config.metric_names_for_computing_utilization().end());

  // Resolve the endpoint-picking child policy.
  Upstream::TypedLoadBalancerFactory* endpoint_picking_policy_factory = nullptr;
  for (const auto& endpoint_picking_policy : lb_config.endpoint_picking_policy().policies()) {
    endpoint_picking_policy_factory =
        Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(
            endpoint_picking_policy.typed_extension_config(),
            /*is_optional=*/true);

    if (endpoint_picking_policy_factory != nullptr) {
      // Self-nesting double-registers stats and drops the outer instance's metric names.
      if (endpoint_picking_policy_factory->name() == name()) {
        return absl::InvalidArgumentError(
            "load_aware_locality cannot be its own endpoint_picking_policy");
      }
      // Load and validate the child policy configuration.
      auto sub_lb_proto_message = endpoint_picking_policy_factory->createEmptyConfigProto();
      RETURN_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
          endpoint_picking_policy.typed_extension_config().typed_config(),
          context.messageValidationVisitor(), *sub_lb_proto_message));

      auto lb_config_or_error =
          endpoint_picking_policy_factory->loadConfig(context, *sub_lb_proto_message);
      RETURN_IF_NOT_OK(lb_config_or_error.status());

      return std::make_unique<LoadAwareLocalityLbConfig>(
          *endpoint_picking_policy_factory,
          LoadBalancerConfigSharedPtr(std::move(lb_config_or_error.value())), weight_update_period,
          utilization_variance_threshold, ewma_alpha, remote_probe_fraction,
          weight_expiration_period, std::move(metric_names), context.mainThreadDispatcher(),
          context.threadLocal());
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
