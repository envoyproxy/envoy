#include "source/extensions/filters/network/redis_proxy/config.h"

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.validate.h"

#include "source/extensions/common/dynamic_forward_proxy/dns_cache_manager_impl.h"
#include "source/extensions/common/redis/cluster_refresh_manager_impl.h"
#include "source/extensions/filters/network/common/redis/client_impl.h"
#include "source/extensions/filters/network/common/redis/fault_impl.h"
#include "source/extensions/filters/network/redis_proxy/command_splitter_impl.h"
#include "source/extensions/filters/network/redis_proxy/hash_slot_router_impl.h"
#include "source/extensions/filters/network/redis_proxy/proxy_filter.h"
#include "source/extensions/filters/network/redis_proxy/prefix_router_impl.h"
#include "source/extensions/filters/network/redis_proxy/router.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

namespace {
inline void addUniqueClustersPrefixRoutes(
    absl::flat_hash_set<std::string>& clusters,
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes::Route&
        route) {
  clusters.emplace(route.cluster());
  for (auto& mirror : route.request_mirror_policy()) {
    clusters.emplace(mirror.cluster());
  }
}

inline void addUniqueClustersHashSlotRoutes(
    absl::flat_hash_set<std::string>& clusters,
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HashSlotRoutes::Route&
        route) {
  clusters.emplace(route.cluster());
  for (auto& mirror : route.request_mirror_policy()) {
    clusters.emplace(mirror.cluster());
  }
}
} // namespace

Network::FilterFactoryCb RedisProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy& proto_config,
    Server::Configuration::FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());
  ASSERT(proto_config.has_settings());

  Extensions::Common::Redis::ClusterRefreshManagerSharedPtr refresh_manager =
      Extensions::Common::Redis::getClusterRefreshManager(
          context.singletonManager(), context.mainThreadDispatcher(), context.clusterManager(),
          context.timeSource());

  Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactoryImpl cache_manager_factory(
      context);
  auto filter_config =
      std::make_shared<ProxyFilterConfig>(proto_config, context.scope(), context.drainDecision(),
                                          context.runtime(), context.api(), cache_manager_factory);

  absl::flat_hash_set<std::string> unique_clusters;

  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::PrefixRoutes prefix_routes;
  envoy::extensions::filters::network::redis_proxy::v3::RedisProxy::HashSlotRoutes hash_slot_routes;

  // Add upstream clusters
  if (proto_config.has_prefix_routes()) {
    prefix_routes = proto_config.prefix_routes();
    // Set the catch-all route from the settings parameters.
    if (prefix_routes.routes_size() == 0 && !prefix_routes.has_catch_all_route()) {
      throw EnvoyException("cannot configure a redis-proxy without any upstream");
    }

    for (auto& route : prefix_routes.routes()) {
      addUniqueClustersPrefixRoutes(unique_clusters, route);
    }
    addUniqueClustersPrefixRoutes(unique_clusters, prefix_routes.catch_all_route());
  } else if (proto_config.has_hash_slot_routes()) {
    hash_slot_routes = proto_config.hash_slot_routes();
    // Set the catch-all route from the settings parameters.
    if (hash_slot_routes.routes_size() == 0 && !hash_slot_routes.has_catch_all_route()) {
      throw EnvoyException("cannot configure a redis-proxy without any upstream");
    }

    for (auto& route : hash_slot_routes.routes()) {
      addUniqueClustersHashSlotRoutes(unique_clusters, route);
    }
    addUniqueClustersHashSlotRoutes(unique_clusters, hash_slot_routes.catch_all_route());
  } else {
    throw EnvoyException("please define a router for upstream clusters");
  }

  RouterPtr router;
  Upstreams upstreams;
  auto redis_command_stats =
      Common::Redis::RedisCommandStats::createRedisCommandStats(context.scope().symbolTable());
  for (auto& cluster : unique_clusters) {
    Stats::ScopeSharedPtr stats_scope =
        context.scope().createScope(fmt::format("cluster.{}.redis_cluster", cluster));
    auto conn_pool_ptr = std::make_shared<ConnPool::InstanceImpl>(
        cluster, context.clusterManager(), Common::Redis::Client::ClientFactoryImpl::instance_,
        context.threadLocal(), proto_config.settings(), context.api(), std::move(stats_scope),
        redis_command_stats, refresh_manager, filter_config->dns_cache_);
    conn_pool_ptr->init();
    upstreams.emplace(cluster, conn_pool_ptr);
  }

  if (proto_config.has_prefix_routes()) {
    router = std::make_unique<PrefixRoutes>(prefix_routes, std::move(upstreams), context.runtime());
  } else {
    router = std::make_unique<HashSlotRoutes>(hash_slot_routes, std::move(upstreams), context.runtime());
  }

  auto fault_manager = std::make_unique<Common::Redis::FaultManagerImpl>(
      context.api().randomGenerator(), context.runtime(), proto_config.faults());

  std::shared_ptr<CommandSplitter::Instance> splitter =
      std::make_shared<CommandSplitter::InstanceImpl>(
          std::move(router), context.scope(), filter_config->stat_prefix_, context.timeSource(),
          proto_config.latency_in_micros(), std::move(fault_manager));
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
