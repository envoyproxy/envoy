#include "source/common/upstream/cluster_factory_impl.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/server/options.h"

#include "source/common/http/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/dns_resolver/dns_factory_util.h"
#include "source/common/network/resolver_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/upstream/health_checker_impl.h"
#include "source/server/transport_socket_config_impl.h"

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
    Event::Dispatcher& dispatcher, AccessLog::AccessLogManager& log_manager,
    const LocalInfo::LocalInfo& local_info, Server::Admin& admin,
    Singleton::Manager& singleton_manager, Outlier::EventLoggerSharedPtr outlier_event_logger,
    bool added_via_api, ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api,
    const Server::Options& options) {
  std::string cluster_type;

  if (!cluster.has_cluster_type()) {
    switch (cluster.type()) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case envoy::config::cluster::v3::Cluster::STATIC:
      cluster_type = "envoy.cluster.static";
      break;
    case envoy::config::cluster::v3::Cluster::STRICT_DNS:
      cluster_type = "envoy.cluster.strict_dns";
      break;
    case envoy::config::cluster::v3::Cluster::LOGICAL_DNS:
      cluster_type = "envoy.cluster.logical_dns";
      break;
    case envoy::config::cluster::v3::Cluster::ORIGINAL_DST:
      cluster_type = "envoy.cluster.original_dst";
      break;
    case envoy::config::cluster::v3::Cluster::EDS:
      cluster_type = "envoy.cluster.eds";
      break;
    }
  } else {
    cluster_type = cluster.cluster_type().name();
  }

  if (cluster.common_lb_config().has_consistent_hashing_lb_config() &&
      cluster.common_lb_config().consistent_hashing_lb_config().use_hostname_for_hashing() &&
      cluster.type() != envoy::config::cluster::v3::Cluster::STRICT_DNS) {
    throw EnvoyException(fmt::format(
        "Cannot use hostname for consistent hashing loadbalancing for cluster of type: '{}'",
        cluster_type));
  }
  ClusterFactory* factory = Registry::FactoryRegistry<ClusterFactory>::getFactory(cluster_type);

  if (factory == nullptr) {
    throw EnvoyException(fmt::format(
        "Didn't find a registered cluster factory implementation for name: '{}'", cluster_type));
  }

  ClusterFactoryContextImpl context(
      cluster_manager, stats, tls, std::move(dns_resolver), ssl_context_manager, runtime,
      dispatcher, log_manager, local_info, admin, singleton_manager,
      std::move(outlier_event_logger), added_via_api, validation_visitor, api, options);
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
  if ((cluster.has_typed_dns_resolver_config() &&
       !(cluster.typed_dns_resolver_config().typed_config().type_url().empty())) ||
      (cluster.has_dns_resolution_config() &&
       !cluster.dns_resolution_config().resolvers().empty()) ||
      !cluster.dns_resolvers().empty()) {

    envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
    Network::DnsResolverFactory& dns_resolver_factory =
        Network::createDnsResolverFactoryFromProto(cluster, typed_dns_resolver_config);
    return dns_resolver_factory.createDnsResolver(context.mainThreadDispatcher(), context.api(),
                                                  typed_dns_resolver_config);
  }

  return context.dnsResolver();
}

std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>
ClusterFactoryImplBase::create(const envoy::config::cluster::v3::Cluster& cluster,
                               ClusterFactoryContext& context) {
  auto stats_scope = generateStatsScope(cluster, context.stats());
  std::unique_ptr<Server::Configuration::TransportSocketFactoryContextImpl>
      transport_factory_context =
          std::make_unique<Server::Configuration::TransportSocketFactoryContextImpl>(
              context.admin(), context.sslContextManager(), *stats_scope, context.clusterManager(),
              context.localInfo(), context.mainThreadDispatcher(), context.stats(),
              context.singletonManager(), context.threadLocal(), context.messageValidationVisitor(),
              context.api(), context.options(), context.logManager());

  std::pair<ClusterImplBaseSharedPtr, ThreadAwareLoadBalancerPtr> new_cluster_pair =
      createClusterImpl(cluster, context, *transport_factory_context, std::move(stats_scope));

  if (!cluster.health_checks().empty()) {
    // TODO(htuch): Need to support multiple health checks in v2.
    if (cluster.health_checks().size() != 1) {
      throw EnvoyException("Multiple health checks not supported");
    } else {
      new_cluster_pair.first->setHealthChecker(HealthCheckerFactory::create(
          cluster.health_checks()[0], *new_cluster_pair.first, context.runtime(),
          context.mainThreadDispatcher(), context.logManager(), context.messageValidationVisitor(),
          context.api()));
    }
  }

  new_cluster_pair.first->setOutlierDetector(Outlier::DetectorImplFactory::createForCluster(
      *new_cluster_pair.first, cluster, context.mainThreadDispatcher(), context.runtime(),
      context.outlierEventLogger(), context.api().randomGenerator()));

  new_cluster_pair.first->setTransportFactoryContext(std::move(transport_factory_context));
  return new_cluster_pair;
}

} // namespace Upstream
} // namespace Envoy
