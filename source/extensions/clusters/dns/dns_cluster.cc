#include "source/extensions/clusters/dns/dns_cluster.h"

#include <chrono>

#include "envoy/common/exception.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/extensions/clusters/dns/v3/dns_cluster.pb.h"

#include "source/common/network/dns_resolver/dns_factory_util.h"
#include "source/extensions/clusters/common/dns_cluster_backcompat.h"
#include "source/extensions/clusters/logical_dns/logical_dns_cluster.h"
#include "source/extensions/clusters/strict_dns/strict_dns_cluster.h"

namespace Envoy {
namespace Upstream {

absl::StatusOr<std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>>
DnsClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::dns::v3::DnsCluster& proto_config,
    Upstream::ClusterFactoryContext& context) {

  absl::StatusOr<Network::DnsResolverSharedPtr> dns_resolver_or_error;
  if (proto_config.has_typed_dns_resolver_config()) {
    Network::DnsResolverFactory& dns_resolver_factory =
        Network::createDnsResolverFactoryFromTypedConfig(proto_config.typed_dns_resolver_config());
    auto& server_context = context.serverFactoryContext();
    dns_resolver_or_error = dns_resolver_factory.createDnsResolver(
        server_context.mainThreadDispatcher(), server_context.api(),
        proto_config.typed_dns_resolver_config());
  } else {
    dns_resolver_or_error = context.dnsResolver();
  }
  RETURN_IF_NOT_OK(dns_resolver_or_error.status());
  absl::StatusOr<std::unique_ptr<ClusterImplBase>> cluster_or_error;
  if (proto_config.logical()) {
    cluster_or_error = LogicalDnsCluster::create(cluster, proto_config, context,
                                                 std::move(*dns_resolver_or_error));
  } else {
    cluster_or_error = StrictDnsClusterImpl::create(cluster, proto_config, context,
                                                    std::move(*dns_resolver_or_error));
  }

  RETURN_IF_NOT_OK(cluster_or_error.status());
  return std::make_pair(std::shared_ptr<ClusterImplBase>(std::move(*cluster_or_error)), nullptr);
}

/**
 * Static registration for the dns cluster factory. @see RegisterFactory.
 */
REGISTER_FACTORY(DnsClusterFactory, Upstream::ClusterFactory);

} // namespace Upstream
} // namespace Envoy
