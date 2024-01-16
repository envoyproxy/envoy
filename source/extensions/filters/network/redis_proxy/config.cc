#include "source/extensions/filters/network/redis_proxy/config.h"

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"

#include "source/extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"
#include "source/extensions/common/redis/cluster_refresh_manager_impl.h"
#include "source/extensions/filters/network/common/redis/client_impl.h"
#include "source/extensions/filters/network/common/redis/fault_impl.h"
#include "source/extensions/filters/network/redis_proxy/command_splitter_impl.h"
#include "source/extensions/filters/network/redis_proxy/proxy_filter.h"
#include "source/extensions/filters/network/redis_proxy/router_impl.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

namespace {
inline void addUniqueClusters(
    absl::flat_hash_set<std::string>& clusters,
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes::Route&
        route) {
  clusters.emplace(route.cluster());
  for (auto& mirror : route.request_mirror_policy()) {
    clusters.emplace(mirror.cluster());
  }
  if (route.has_read_command_policy()) {
    clusters.emplace(route.read_command_policy().cluster());
  }
}
} // namespace

Network::FilterFactoryCb RedisProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy& proto_config,
    Server::Configuration::FactoryContext& context) {

  auto& server_context = context.serverFactoryContext();

  ASSERT(!proto_config.stat_prefix().empty());
  ASSERT(proto_config.has_settings());

  Extensions::Common::Redis::ClusterRefreshManagerSharedPtr refresh_manager =
      Extensions::Common::Redis::getClusterRefreshManager(
          server_context.singletonManager(), server_context.mainThreadDispatcher(),
          server_context.clusterManager(), server_context.timeSource());

  Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactoryImpl cache_manager_factory(
      context);

  auto filter_config = std::make_shared<ProxyFilterConfig>(
      proto_config, context.scope(), context.drainDecision(), server_context.runtime(),
      server_context.api(), cache_manager_factory);

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes prefix_routes(
      proto_config.prefix_routes());

  // Set the catch-all route from the settings parameters.
  if (prefix_routes.routes_size() == 0 && !prefix_routes.has_catch_all_route()) {
    throw EnvoyException("cannot configure a redis-proxy without any upstream");
  }

  absl::flat_hash_set<std::string> unique_clusters;
  for (auto& route : prefix_routes.routes()) {
    addUniqueClusters(unique_clusters, route);
  }
  addUniqueClusters(unique_clusters, prefix_routes.catch_all_route());

  auto redis_command_stats =
      Common::Redis::RedisCommandStats::createRedisCommandStats(context.scope().symbolTable());

  Upstreams upstreams;
  for (auto& cluster : unique_clusters) {
    Stats::ScopeSharedPtr stats_scope =
        context.scope().createScope(fmt::format("cluster.{}.redis_cluster", cluster));
    auto conn_pool_ptr = std::make_shared<ConnPool::InstanceImpl>(
        cluster, server_context.clusterManager(),
        Common::Redis::Client::ClientFactoryImpl::instance_, server_context.threadLocal(),
        proto_config.settings(), server_context.api(), std::move(stats_scope), redis_command_stats,
        refresh_manager, filter_config->dns_cache_);
    conn_pool_ptr->init();
    upstreams.emplace(cluster, conn_pool_ptr);
  }

  auto router =
      std::make_unique<PrefixRoutes>(prefix_routes, std::move(upstreams), server_context.runtime());

  auto fault_manager = std::make_unique<Common::Redis::FaultManagerImpl>(
      server_context.api().randomGenerator(), server_context.runtime(), proto_config.faults());

  std::shared_ptr<CommandSplitter::Instance> splitter =
      std::make_shared<CommandSplitter::InstanceImpl>(
          std::move(router), context.scope(), filter_config->stat_prefix_,
          server_context.timeSource(), proto_config.latency_in_micros(), std::move(fault_manager));
  return [splitter, filter_config](Network::FilterManager& filter_manager) -> void {
    Common::Redis::DecoderFactoryImpl factory;
    filter_manager.addReadFilter(std::make_shared<ProxyFilter>(
        factory, Common::Redis::EncoderPtr{new Common::Redis::EncoderImpl()}, *splitter,
        filter_config));
  };
}

/**
 * Static registration for the redis filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(RedisProxyFilterConfigFactory,
                        Server::Configuration::NamedNetworkFilterConfigFactory,
                        "envoy.redis_proxy");

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
