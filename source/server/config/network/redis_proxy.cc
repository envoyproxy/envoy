#include "server/config/network/redis_proxy.h"

#include <memory>
#include <string>

#include "envoy/registry/registry.h"

#include "common/redis/codec_impl.h"
#include "common/redis/command_splitter_impl.h"
#include "common/redis/conn_pool_impl.h"
#include "common/redis/proxy_filter.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb
RedisProxyFilterConfigFactory::createFilterFactory(const Json::Object& config,
                                                   FactoryContext& context) {
  Redis::ProxyFilterConfigSharedPtr filter_config(std::make_shared<Redis::ProxyFilterConfig>(
      config, context.clusterManager(), context.scope()));
  Redis::ConnPool::InstancePtr conn_pool(
      new Redis::ConnPool::InstanceImpl(filter_config->clusterName(), context.clusterManager(),
                                        Redis::ConnPool::ClientFactoryImpl::instance_,
                                        context.threadLocal(), *config.getObject("conn_pool")));
  std::shared_ptr<Redis::CommandSplitter::Instance> splitter(
      new Redis::CommandSplitter::InstanceImpl(std::move(conn_pool), context.scope(),
                                               filter_config->statPrefix()));
  return [splitter, filter_config](Network::FilterManager& filter_manager) -> void {
    Redis::DecoderFactoryImpl factory;
    filter_manager.addReadFilter(std::make_shared<Redis::ProxyFilter>(
        factory, Redis::EncoderPtr{new Redis::EncoderImpl()}, *splitter, filter_config));
  };
}

/**
 * Static registration for the redis filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RedisProxyFilterConfigFactory, NamedNetworkFilterConfigFactory>
    registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
