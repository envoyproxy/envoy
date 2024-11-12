#include "source/extensions/clusters/dns/dns_cluster.h"

#include <chrono>

#include "envoy/common/exception.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/extensions/clusters/dns/v3/dns_cluster.pb.h"

#include "source/extensions/clusters/common/dns_cluster_backcompat.h"

namespace Envoy {
namespace Upstream {

absl::StatusOr<std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>>

DnsClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::dns::v3::DnsCluster& proto_config,
    Upstream::ClusterFactoryContext& context) {
  std::string cluster_type_name = "";
  switch (proto_config.dns_discovery_type()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::clusters::dns::v3::DnsCluster::STRICT:
    cluster_type_name = "envoy.cluster.strict_dns";
    break;
  case envoy::extensions::clusters::dns::v3::DnsCluster::LOGICAL:
    cluster_type_name = "envoy.cluster.logical_dns";
    break;
  }
  ClusterFactory* factory =
      Registry::FactoryRegistry<ClusterFactory>::getFactory(cluster_type_name);

  if (factory == nullptr) {
    return absl::InvalidArgumentError(
        fmt::format("Didn't find a registered cluster factory implementation for name: '{}'",
                    cluster_type_name));
  }
  auto dns_cluster = factory->create(cluster, context);
  if (!dns_cluster.ok()) {
    return dns_cluster.status();
  }
  return std::make_pair(std::dynamic_pointer_cast<ClusterImplBase>(dns_cluster->first),
                        std::move(dns_cluster->second));
}

/**
 * Static registration for the strict dns cluster factory. @see RegisterFactory.
 */
REGISTER_FACTORY(DnsClusterFactory, Upstream::ClusterFactory);

} // namespace Upstream
} // namespace Envoy
