#include "extensions/filters/network/redis_proxy/config.h"

#include <memory>
#include <string>

#include "envoy/config/filter/network/redis_proxy/v2/redis_proxy.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"

#include "extensions/filters/network/common/redis/client_impl.h"
#include "extensions/filters/network/common/redis/codec_impl.h"
#include "extensions/filters/network/redis_proxy/command_splitter_impl.h"
#include "extensions/filters/network/redis_proxy/proxy_filter.h"
#include "extensions/filters/network/redis_proxy/router_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

Network::FilterFactoryCb RedisProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::network::redis_proxy::v2::RedisProxy& proto_config,
    Server::Configuration::FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());
  ASSERT(proto_config.has_settings());

  ProxyFilterConfigSharedPtr filter_config(std::make_shared<ProxyFilterConfig>(
      proto_config, context.scope(), context.drainDecision(), context.runtime(), context.api()));

  envoy::config::filter::network::redis_proxy::v2::RedisProxy::PrefixRoutes prefix_routes(
      proto_config.prefix_routes());

  // set the catch-all route from the deprecated cluster and settings parameters.
  if (prefix_routes.catch_all_cluster().empty() && prefix_routes.routes_size() == 0) {
    if (proto_config.cluster().empty()) {
      throw EnvoyException("cannot configure a redis-proxy without any upstream");
    }

    prefix_routes.set_catch_all_cluster(proto_config.cluster());
  }

  std::set<std::string> unique_clusters;
  for (auto& route : prefix_routes.routes()) {
    unique_clusters.emplace(route.cluster());
  }
  unique_clusters.emplace(prefix_routes.catch_all_cluster());

  Upstreams upstreams;
  for (auto& cluster : unique_clusters) {
    upstreams.emplace(cluster, std::make_shared<ConnPool::InstanceImpl>(
                                   cluster, context.clusterManager(),
                                   Common::Redis::Client::ClientFactoryImpl::instance_,
                                   context.threadLocal(), proto_config.settings(), context.api(),
                                   context.scope().symbolTable()));
  }

  auto router = std::make_unique<PrefixRoutes>(prefix_routes, std::move(upstreams));

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
