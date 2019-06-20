#include "common/upstream/logical_dns_cluster.h"

#include <chrono>
#include <list>
#include <string>
#include <vector>

#include "envoy/stats/scope.h"

#include "common/common/fmt.h"
#include "common/config/utility.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

LogicalDnsCluster::LogicalDnsCluster(
    const envoy::api::v2::Cluster& cluster, Runtime::Loader& runtime,
    Network::DnsResolverSharedPtr dns_resolver,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    Stats::ScopePtr&& stats_scope, bool added_via_api)
    : ClusterImplBase(cluster, runtime, factory_context, std::move(stats_scope), added_via_api),
      dns_resolver_(dns_resolver),
      dns_refresh_rate_ms_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(cluster, dns_refresh_rate, 5000))),
      respect_dns_ttl_(cluster.respect_dns_ttl()),
      resolve_timer_(
          factory_context.dispatcher().createTimer([this]() -> void { startResolve(); })),
      local_info_(factory_context.localInfo()),
      load_assignment_(cluster.has_load_assignment()
                           ? cluster.load_assignment()
                           : Config::Utility::translateClusterHosts(cluster.hosts())) {
  const auto& locality_lb_endpoints = load_assignment_.endpoints();
  if (locality_lb_endpoints.size() != 1 || locality_lb_endpoints[0].lb_endpoints().size() != 1) {
    if (cluster.has_load_assignment()) {
      throw EnvoyException(
          "LOGICAL_DNS clusters must have a single locality_lb_endpoint and a single lb_endpoint");
    } else {
      throw EnvoyException("LOGICAL_DNS clusters must have a single host");
    }
  }

  const envoy::api::v2::core::SocketAddress& socket_address =
      lbEndpoint().endpoint().address().socket_address();

  if (!socket_address.resolver_name().empty()) {
    throw EnvoyException("LOGICAL_DNS clusters must NOT have a custom resolver name set");
  }

  dns_url_ = fmt::format("tcp://{}:{}", socket_address.address(), socket_address.port_value());
  hostname_ = Network::Utility::hostFromTcpUrl(dns_url_);
  dns_lookup_family_ = getDnsLookupFamilyFromCluster(cluster);

  if (absl::EndsWithIgnoreCase(dns_url_, ":srv")) {
    srv_ = true;
  } else {
    Network::Utility::portFromTcpUrl(dns_url_);
  }
}

void LogicalDnsCluster::startPreInit() { startResolve(); }

LogicalDnsCluster::~LogicalDnsCluster() {
  if (active_dns_query_) {
    active_dns_query_->cancel();
  }
}

void LogicalDnsCluster::startResolve() {
  std::string dns_address = Network::Utility::hostFromTcpUrl(dns_url_);
  ENVOY_LOG(debug, "starting async DNS resolution for {}", dns_address);
  info_->stats().update_attempt_.inc();

  if (srv_) {
    active_dns_query_ = dns_resolver_->resolveSrv(
        dns_address, dns_lookup_family_,
        [this, dns_address](std::list<Network::DnsResponse>&& response) -> void {
          updateHosts(dns_address, std::move(response),
                      static_cast<std::function<Network::Address::InstanceConstSharedPtr(
                          const Network::Address::SrvInstanceConstSharedPtr&)>>(
                          [](const Network::Address::SrvInstanceConstSharedPtr& srv) {
                            return srv->address();
                          }));
        });
  } else {
    active_dns_query_ = dns_resolver_->resolve(
        dns_address, dns_lookup_family_,
        [this, dns_address](
            const std::list<Network::Address::InstanceConstSharedPtr>&& address_list) -> void {
          updateHosts(dns_address, std::move(address_list),
                      static_cast<std::function<Network::Address::InstanceConstSharedPtr(
                          const Network::Address::InstanceConstSharedPtr&)>>(
                          [this](const Network::Address::InstanceConstSharedPtr& address) {
                            return Network::Utility::getAddressWithPort(
                                *address, Network::Utility::portFromTcpUrl(dns_url_));
                          }));
        });
  }
}

std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr>
LogicalDnsClusterFactory::createClusterImpl(
    const envoy::api::v2::Cluster& cluster, ClusterFactoryContext& context,
    Server::Configuration::TransportSocketFactoryContext& socket_factory_context,
    Stats::ScopePtr&& stats_scope) {
  auto selected_dns_resolver = selectDnsResolver(cluster, context);

  return std::make_pair(std::make_shared<LogicalDnsCluster>(
                            cluster, context.runtime(), selected_dns_resolver,
                            socket_factory_context, std::move(stats_scope), context.addedViaApi()),
                        nullptr);
}

/**
 * Static registration for the logical dns cluster factory. @see RegisterFactory.
 */
REGISTER_FACTORY(LogicalDnsClusterFactory, ClusterFactory);

} // namespace Upstream
} // namespace Envoy
