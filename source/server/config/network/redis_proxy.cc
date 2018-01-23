#include "server/config/network/redis_proxy.h"

#include <memory>
#include <string>

#include "envoy/api/v2/filter/network/redis_proxy.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/redis/codec_impl.h"
#include "common/redis/command_splitter_impl.h"
#include "common/redis/conn_pool_impl.h"
#include "common/redis/proxy_filter.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb RedisProxyFilterConfigFactory::createFilter(
    const envoy::api::v2::filter::network::RedisProxy& proto_config, FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());
  ASSERT(!proto_config.cluster().empty());
  ASSERT(proto_config.has_settings());

  Redis::ProxyFilterConfigSharedPtr filter_config(std::make_shared<Redis::ProxyFilterConfig>(
      proto_config, context.clusterManager(), context.scope(), context.drainDecision(),
      context.runtime()));
  Redis::ConnPool::InstancePtr conn_pool(
      new Redis::ConnPool::InstanceImpl(filter_config->cluster_name_, context.clusterManager(),
                                        Redis::ConnPool::ClientFactoryImpl::instance_,
                                        context.threadLocal(), proto_config.settings()));
  std::shared_ptr<Redis::CommandSplitter::Instance> splitter(
      new Redis::CommandSplitter::InstanceImpl(std::move(conn_pool), context.scope(),
                                               filter_config->stat_prefix_));
  return [splitter, filter_config](Network::FilterManager& filter_manager) -> void {
    Redis::DecoderFactoryImpl factory;
    filter_manager.addReadFilter(std::make_shared<Redis::ProxyFilter>(
        factory, Redis::EncoderPtr{new Redis::EncoderImpl()}, *splitter, filter_config));
  };
}

NetworkFilterFactoryCb
RedisProxyFilterConfigFactory::createFilterFactory(const Json::Object& json_config,
                                                   FactoryContext& context) {
  envoy::api::v2::filter::network::RedisProxy proto_config;
  Config::FilterJson::translateRedisProxy(json_config, proto_config);
  return createFilter(proto_config, context);
}

NetworkFilterFactoryCb
RedisProxyFilterConfigFactory::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                            FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<const envoy::api::v2::filter::network::RedisProxy&>(
          proto_config),
      context);
}

/**
 * Static registration for the redis filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RedisProxyFilterConfigFactory, NamedNetworkFilterConfigFactory>
    registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
