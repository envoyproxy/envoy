#include "source/common/upstream/endpoint_config_dump.h"

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"

#include "source/common/network/utility.h"

namespace Envoy {
namespace Upstream {
namespace {

void addLbEndpoint(const Upstream::HostSharedPtr& host,
                   envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint) {
  auto& lb_endpoint = *locality_lb_endpoint.mutable_lb_endpoints()->Add();
  if (host->metadata() != nullptr) {
    lb_endpoint.mutable_metadata()->MergeFrom(*host->metadata());
  }
  lb_endpoint.mutable_load_balancing_weight()->set_value(host->weight());

  switch (host->coarseHealth()) {
  case Upstream::Host::Health::Healthy:
    lb_endpoint.set_health_status(envoy::config::core::v3::HealthStatus::HEALTHY);
    break;
  case Upstream::Host::Health::Unhealthy:
    lb_endpoint.set_health_status(envoy::config::core::v3::HealthStatus::UNHEALTHY);
    break;
  case Upstream::Host::Health::Degraded:
    lb_endpoint.set_health_status(envoy::config::core::v3::HealthStatus::DEGRADED);
    break;
  default:
    lb_endpoint.set_health_status(envoy::config::core::v3::HealthStatus::UNKNOWN);
  }

  auto& endpoint = *lb_endpoint.mutable_endpoint();
  endpoint.set_hostname(host->hostname());
  Network::Utility::addressToProtobufAddress(*host->address(), *endpoint.mutable_address());
  if (host->addressListOrNull() != nullptr) {
    const auto& address_list = *host->addressListOrNull();
    if (address_list.size() > 1) {
      // skip first address of the list as the default address is not an additional one.
      for (auto it = std::next(address_list.begin()); it != address_list.end(); ++it) {
        auto& new_address = *endpoint.mutable_additional_addresses()->Add();
        Network::Utility::addressToProtobufAddress(**it, *new_address.mutable_address());
      }
    }
  }
  auto& health_check_config = *endpoint.mutable_health_check_config();
  health_check_config.set_hostname(host->hostnameForHealthChecks());
  if (host->healthCheckAddress()->asString() != host->address()->asString()) {
    health_check_config.set_port_value(host->healthCheckAddress()->ip()->port());
  }
}

} // namespace

ProtobufTypes::MessagePtr dumpEndpointConfigs(const Upstream::ClusterManager& cluster_manager,
                                              const Matchers::StringMatcher& name_matcher) {
  auto endpoint_config_dump = std::make_unique<envoy::admin::v3::EndpointsConfigDump>();
  // TODO(mattklein123): Add ability to see warming clusters in admin output.
  cluster_manager.forEachActiveCluster([&](const Upstream::Cluster& cluster) {
    Upstream::ClusterInfoConstSharedPtr cluster_info = cluster.info();
    envoy::config::endpoint::v3::ClusterLoadAssignment cluster_load_assignment;

    if (!cluster_info->edsServiceName().empty()) {
      cluster_load_assignment.set_cluster_name(cluster_info->edsServiceName());
    } else {
      cluster_load_assignment.set_cluster_name(cluster_info->name());
    }
    if (!name_matcher.match(cluster_load_assignment.cluster_name())) {
      return;
    }
    auto& policy = *cluster_load_assignment.mutable_policy();

    // Using MILLION as denominator in config dump.
    float value = cluster.dropOverload().value() * 1000000;
    if (value > 0) {
      auto* drop_overload = policy.add_drop_overloads();
      drop_overload->set_category(cluster.dropCategory());
      auto* percent = drop_overload->mutable_drop_percentage();
      percent->set_denominator(envoy::type::v3::FractionalPercent::MILLION);
      percent->set_numerator(uint32_t(value));
    }

    for (auto& host_set : cluster.prioritySet().hostSetsPerPriority()) {
      policy.mutable_overprovisioning_factor()->set_value(host_set->overprovisioningFactor());

      if (!host_set->hostsPerLocality().get().empty()) {
        for (int index = 0; index < static_cast<int>(host_set->hostsPerLocality().get().size());
             index++) {
          auto locality_host_set = host_set->hostsPerLocality().get()[index];

          if (!locality_host_set.empty()) {
            auto& locality_lb_endpoint = *cluster_load_assignment.mutable_endpoints()->Add();
            locality_lb_endpoint.mutable_locality()->MergeFrom(locality_host_set[0]->locality());
            locality_lb_endpoint.set_priority(locality_host_set[0]->priority());
            if (host_set->localityWeights() != nullptr && !host_set->localityWeights()->empty()) {
              locality_lb_endpoint.mutable_load_balancing_weight()->set_value(
                  (*host_set->localityWeights())[index]);
            }

            for (auto& host : locality_host_set) {
              addLbEndpoint(host, locality_lb_endpoint);
            }
          }
        }
      } else {
        for (auto& host : host_set->hosts()) {
          auto& locality_lb_endpoint = *cluster_load_assignment.mutable_endpoints()->Add();
          locality_lb_endpoint.mutable_locality()->MergeFrom(host->locality());
          locality_lb_endpoint.set_priority(host->priority());
          addLbEndpoint(host, locality_lb_endpoint);
        }
      }
    }
    if (cluster_info->addedViaApi()) {
      auto& dynamic_endpoint = *endpoint_config_dump->mutable_dynamic_endpoint_configs()->Add();
      std::ignore = dynamic_endpoint.mutable_endpoint_config()->PackFrom(cluster_load_assignment);
    } else {
      auto& static_endpoint = *endpoint_config_dump->mutable_static_endpoint_configs()->Add();
      std::ignore = static_endpoint.mutable_endpoint_config()->PackFrom(cluster_load_assignment);
    }
  });
  return endpoint_config_dump;
}

} // namespace Upstream
} // namespace Envoy
