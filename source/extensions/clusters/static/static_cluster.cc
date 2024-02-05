#include "source/extensions/clusters/static/static_cluster.h"

#include "envoy/common/exception.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"

namespace Envoy {
namespace Upstream {

StaticClusterImpl::StaticClusterImpl(const envoy::config::cluster::v3::Cluster& cluster,
                                     ClusterFactoryContext& context)
    : ClusterImplBase(cluster, context),
      priority_state_manager_(
          new PriorityStateManager(*this, context.serverFactoryContext().localInfo(), nullptr)) {
  const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment =
      cluster.load_assignment();
  overprovisioning_factor_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      cluster_load_assignment.policy(), overprovisioning_factor, kDefaultOverProvisioningFactor);
  weighted_priority_health_ = cluster_load_assignment.policy().weighted_priority_health();

  Event::Dispatcher& dispatcher = context.serverFactoryContext().mainThreadDispatcher();

  for (const auto& locality_lb_endpoint : cluster_load_assignment.endpoints()) {
    validateEndpointsForZoneAwareRouting(locality_lb_endpoint);
    priority_state_manager_->initializePriorityFor(locality_lb_endpoint);
    for (const auto& lb_endpoint : locality_lb_endpoint.lb_endpoints()) {
      priority_state_manager_->registerHostForPriority(
          lb_endpoint.endpoint().hostname(), resolveProtoAddress(lb_endpoint.endpoint().address()),
          {}, locality_lb_endpoint, lb_endpoint, dispatcher.timeSource());
    }
  }
}

void StaticClusterImpl::startPreInit() {
  // At this point see if we have a health checker. If so, mark all the hosts unhealthy and
  // then fire update callbacks to start the health checking process.
  const auto& health_checker_flag =
      health_checker_ != nullptr
          ? absl::optional<Upstream::Host::HealthFlag>(Host::HealthFlag::FAILED_ACTIVE_HC)
          : absl::nullopt;

  auto& priority_state = priority_state_manager_->priorityState();
  for (size_t i = 0; i < priority_state.size(); ++i) {
    if (priority_state[i].first == nullptr) {
      priority_state[i].first = std::make_unique<HostVector>();
    }
    priority_state_manager_->updateClusterPrioritySet(
        i, std::move(priority_state[i].first), absl::nullopt, absl::nullopt, health_checker_flag,
        weighted_priority_health_, overprovisioning_factor_);
  }
  priority_state_manager_.reset();

  onPreInitComplete();
}

absl::StatusOr<std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>>
StaticClusterFactory::createClusterImpl(const envoy::config::cluster::v3::Cluster& cluster,
                                        ClusterFactoryContext& context) {
  const envoy::config::endpoint::v3::ClusterLoadAssignment& cluster_load_assignment =
      cluster.load_assignment();
  for (const auto& locality_lb_endpoint : cluster_load_assignment.endpoints()) {
    // TODO(adisuissa): Implement LEDS support for STATIC clusters.
    if (locality_lb_endpoint.has_leds_cluster_locality_config()) {
      return absl::InvalidArgumentError(
          fmt::format("LEDS is only supported when EDS is used. Static cluster {} cannot use LEDS.",
                      cluster.name()));
    }
  }
  return std::make_pair(std::shared_ptr<StaticClusterImpl>(new StaticClusterImpl(cluster, context)),
                        nullptr);
}

/**
 * Static registration for the static cluster factory. @see RegisterFactory.
 */
REGISTER_FACTORY(StaticClusterFactory, ClusterFactory);

} // namespace Upstream
} // namespace Envoy
