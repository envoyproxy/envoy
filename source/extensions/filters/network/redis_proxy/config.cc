#include "extensions/filters/network/redis_proxy/config.h"

#include <memory>
#include <string>

#include "envoy/config/filter/network/redis_proxy/v2/redis_proxy.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"

#include "extensions/filters/network/redis_proxy/codec_impl.h"
#include "extensions/filters/network/redis_proxy/command_splitter_impl.h"
#include "extensions/filters/network/redis_proxy/conn_pool_impl.h"
#include "extensions/filters/network/redis_proxy/proxy_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

Network::FilterFactoryCb RedisProxyFilterConfigFactory::createFilter(
    const envoy::config::filter::network::redis_proxy::v2::RedisProxy& proto_config,
    Server::Configuration::FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());
  ASSERT(!proto_config.cluster().empty());
  ASSERT(proto_config.has_settings());

  ProxyFilterConfigSharedPtr filter_config(
      std::make_shared<ProxyFilterConfig>(proto_config, context.clusterManager(), context.scope(),
                                          context.drainDecision(), context.runtime()));
  ConnPool::InstancePtr conn_pool(new ConnPool::InstanceImpl(
      filter_config->cluster_name_, context.clusterManager(),
      ConnPool::ClientFactoryImpl::instance_, context.threadLocal(), proto_config.settings()));
  std::shared_ptr<CommandSplitter::Instance> splitter(new CommandSplitter::InstanceImpl(
      std::move(conn_pool), context.scope(), filter_config->stat_prefix_));
  return [splitter, filter_config](Network::FilterManager& filter_manager) -> void {
    DecoderFactoryImpl factory;
    filter_manager.addReadFilter(std::make_shared<ProxyFilter>(
        factory, EncoderPtr{new EncoderImpl()}, *splitter, filter_config));
  };
}

Network::FilterFactoryCb
RedisProxyFilterConfigFactory::createFilterFactory(const Json::Object& json_config,
                                                   Server::Configuration::FactoryContext& context) {
  envoy::config::filter::network::redis_proxy::v2::RedisProxy proto_config;
  Config::FilterJson::translateRedisProxy(json_config, proto_config);
  return createFilter(proto_config, context);
}

Network::FilterFactoryCb RedisProxyFilterConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, Server::Configuration::FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<
          const envoy::config::filter::network::redis_proxy::v2::RedisProxy&>(proto_config),
      context);
}

/**
 * Static registration for the redis filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RedisProxyFilterConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
