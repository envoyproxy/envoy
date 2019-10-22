#include "extensions/filters/network/redis_proxy/config.h"

#include "common/config/filter_json.h"

#include "extensions/common/redis/redirection_mgr_impl.h"
#include "extensions/filters/network/common/redis/client_impl.h"
#include "extensions/filters/network/redis_proxy/command_splitter_impl.h"
#include "extensions/filters/network/redis_proxy/proxy_filter.h"
#include "extensions/filters/network/redis_proxy/router_impl.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

namespace {
inline void addUniqueClusters(
    absl::flat_hash_set<std::string>& clusters,
    const envoy::config::filter::network::redis_proxy::v2::RedisProxy_PrefixRoutes_Route& route) {
  clusters.emplace(route.cluster());
  for (auto& mirror : route.request_mirror_policy()) {
    clusters.emplace(mirror.cluster());
  }
}
} // namespace

Network::FilterFactoryCb RedisProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::network::redis_proxy::v2::RedisProxy& proto_config,
    Server::Configuration::FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());
  ASSERT(proto_config.has_settings());

  Extensions::Common::Redis::RedirectionManagerSharedPtr redirection_manager =
      Extensions::Common::Redis::getRedirectionManager(
          context.singletonManager(), context.dispatcher(), context.clusterManager(),
          context.timeSource());

  ProxyFilterConfigSharedPtr filter_config(std::make_shared<ProxyFilterConfig>(
      proto_config, context.scope(), context.drainDecision(), context.runtime(), context.api()));

  envoy::config::filter::network::redis_proxy::v2::RedisProxy::PrefixRoutes prefix_routes(
      proto_config.prefix_routes());

  // Set the catch-all route from the deprecated cluster and settings parameters.
  if (prefix_routes.catch_all_cluster().empty() && prefix_routes.routes_size() == 0 &&
      !prefix_routes.has_catch_all_route()) {
    if (proto_config.cluster().empty()) {
      throw EnvoyException("cannot configure a redis-proxy without any upstream");
    }

    prefix_routes.mutable_catch_all_route()->set_cluster(proto_config.cluster());
  } else if (!prefix_routes.catch_all_cluster().empty() && !prefix_routes.has_catch_all_route()) {
    // Set the catch-all route from the deprecated catch-all cluster.
    prefix_routes.mutable_catch_all_route()->set_cluster(prefix_routes.catch_all_cluster());
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
    Stats::ScopePtr stats_scope =
        context.scope().createScope(fmt::format("cluster.{}.redis_cluster", cluster));

    upstreams.emplace(cluster,
                      std::make_shared<ConnPool::InstanceImpl>(
                          cluster, context.clusterManager(),
                          Common::Redis::Client::ClientFactoryImpl::instance_,
                          context.threadLocal(), proto_config.settings(), context.api(),
                          std::move(stats_scope), redis_command_stats, redirection_manager));
  }

  auto router =
      std::make_unique<PrefixRoutes>(prefix_routes, std::move(upstreams), context.runtime());

  std::shared_ptr<CommandSplitter::Instance> splitter =
      std::make_shared<CommandSplitter::InstanceImpl>(
          std::move(router), context.scope(), filter_config->stat_prefix_, context.timeSource(),
          proto_config.latency_in_micros());
  return [splitter, filter_config](Network::FilterManager& filter_manager) -> void {
    Common::Redis::DecoderFactoryImpl factory;
    filter_manager.addReadFilter(std::make_shared<ProxyFilter>(
        factory, Common::Redis::EncoderPtr{new Common::Redis::EncoderImpl()}, *splitter,
        filter_config));
  };
}

Network::FilterFactoryCb
RedisProxyFilterConfigFactory::createFilterFactory(const Json::Object& json_config,
                                                   Server::Configuration::FactoryContext& context) {
  envoy::config::filter::network::redis_proxy::v2::RedisProxy proto_config;
  Config::FilterJson::translateRedisProxy(json_config, proto_config);
  return createFilterFactoryFromProtoTyped(proto_config, context);
}

/**
 * Static registration for the redis filter. @see RegisterFactory.
 */
REGISTER_FACTORY(RedisProxyFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
