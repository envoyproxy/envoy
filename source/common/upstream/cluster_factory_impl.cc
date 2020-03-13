#include "common/upstream/cluster_factory_impl.h"

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "common/http/utility.h"
#include "common/network/address_impl.h"
#include "common/network/resolver_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/upstream/health_checker_impl.h"

#include "server/transport_socket_config_impl.h"

namespace Envoy {
namespace Upstream {

namespace {

Stats::ScopePtr generateStatsScope(const envoy::config::cluster::v3::Cluster& config,
                                   Stats::Store& stats) {
  return stats.createScope(fmt::format(
      "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
}

} // namespace

std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr> ClusterFactoryImplBase::create(
    const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cluster_manager,
    Stats::Store& stats, ThreadLocal::Instance& tls, Network::DnsResolverSharedPtr dns_resolver,
    Ssl::ContextManager& ssl_context_manager, Runtime::Loader& runtime,
    Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
    AccessLog::AccessLogManager& log_manager, const LocalInfo::LocalInfo& local_info,
    Server::Admin& admin, Singleton::Manager& singleton_manager,
    Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api,
    ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api) {
  std::string cluster_type;

  if (!cluster.has_cluster_type()) {
    switch (cluster.type()) {
    case envoy::config::cluster::v3::Cluster::STATIC:
      cluster_type = Extensions::Clusters::ClusterTypes::get().Static;
      break;
    case envoy::config::cluster::v3::Cluster::STRICT_DNS:
      cluster_type = Extensions::Clusters::ClusterTypes::get().StrictDns;
      break;
    case envoy::config::cluster::v3::Cluster::LOGICAL_DNS:
      cluster_type = Extensions::Clusters::ClusterTypes::get().LogicalDns;
      break;
    case envoy::config::cluster::v3::Cluster::ORIGINAL_DST:
      cluster_type = Extensions::Clusters::ClusterTypes::get().OriginalDst;
      break;
    case envoy::config::cluster::v3::Cluster::EDS:
      cluster_type = Extensions::Clusters::ClusterTypes::get().Eds;
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  } else {
    cluster_type = cluster.cluster_type().name();
  }
  ClusterFactory* factory = Registry::FactoryRegistry<ClusterFactory>::getFactory(cluster_type);

  if (factory == nullptr) {
    throw EnvoyException(fmt::format(
        "Didn't find a registered cluster factory implementation for name: '{}'", cluster_type));
  }

  ClusterFactoryContextImpl context(
      cluster_manager, stats, tls, std::move(dns_resolver), ssl_context_manager, runtime, random,
      dispatcher, log_manager, local_info, admin, singleton_manager,
      std::move(outlier_event_logger), added_via_api, validation_visitor, api);
  return factory->create(cluster, context);
}

Network::DnsResolverSharedPtr
ClusterFactoryImplBase::selectDnsResolver(const envoy::config::cluster::v3::Cluster& cluster,
                                          ClusterFactoryContext& context) {
  // We make this a shared pointer to deal with the distinct ownership
  // scenarios that can exist: in one case, we pass in the "default"
  // DNS resolver that is owned by the Server::Instance. In the case
  // where 'dns_resolvers' is specified, we have per-cluster DNS
  // resolvers that are created here but ownership resides with
  // StrictDnsClusterImpl/LogicalDnsCluster.
  if (!cluster.dns_resolvers().empty()) {
    const auto& resolver_addrs = cluster.dns_resolvers();
    std::vector<Network::Address::InstanceConstSharedPtr> resolvers;
    resolvers.reserve(resolver_addrs.size());
    for (const auto& resolver_addr : resolver_addrs) {
      resolvers.push_back(Network::Address::resolveProtoAddress(resolver_addr));
    }
    const bool use_tcp_for_dns_lookups = cluster.use_tcp_for_dns_lookups();
    return context.dispatcher().createDnsResolver(resolvers, use_tcp_for_dns_lookups);
  }

  return context.dnsResolver();
}

std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>
ClusterFactoryImplBase::create(const envoy::config::cluster::v3::Cluster& cluster,
                               ClusterFactoryContext& context) {
  auto stats_scope = generateStatsScope(cluster, context.stats());
  Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      context.admin(), context.sslContextManager(), *stats_scope, context.clusterManager(),
      context.localInfo(), context.dispatcher(), context.random(), context.stats(),
      context.singletonManager(), context.tls(), context.messageValidationVisitor(), context.api());

  std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr> new_cluster_pair =
      createClusterImpl(cluster, context, factory_context, std::move(stats_scope));

  if (!cluster.health_checks().empty()) {
    // TODO(htuch): Need to support multiple health checks in v2.
    if (cluster.health_checks().size() != 1) {
      throw EnvoyException("Multiple health checks not supported");
    } else {
      new_cluster_pair.first->setHealthChecker(HealthCheckerFactory::create(
          cluster.health_checks()[0], *new_cluster_pair.first, context.runtime(), context.random(),
          context.dispatcher(), context.logManager(), context.messageValidationVisitor(),
          context.api()));
    }
  }

  new_cluster_pair.first->setOutlierDetector(Outlier::DetectorImplFactory::createForCluster(
      *new_cluster_pair.first, cluster, context.dispatcher(), context.runtime(),
      context.outlierEventLogger()));
  return new_cluster_pair;
}

} // namespace Upstream
} // namespace Envoy
