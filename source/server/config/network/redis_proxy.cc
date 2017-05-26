#include "server/config/network/redis_proxy.h"

#include <memory>
#include <string>

#include "common/redis/codec_impl.h"
#include "common/redis/command_splitter_impl.h"
#include "common/redis/conn_pool_impl.h"
#include "common/redis/proxy_filter.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb RedisProxyFilterConfigFactory::createFilterFactory(
    NetworkFilterType type, const Json::Object& config, Server::Instance& server) {
  if (type != NetworkFilterType::Read) {
    throw EnvoyException(
        fmt::format("{} network filter must be configured as a read filter.", name()));
  }

  Redis::ProxyFilterConfigSharedPtr filter_config(
      std::make_shared<Redis::ProxyFilterConfig>(config, server.clusterManager(), server.stats()));
  Redis::ConnPool::InstancePtr conn_pool(
      new Redis::ConnPool::InstanceImpl(filter_config->clusterName(), server.clusterManager(),
                                        Redis::ConnPool::ClientFactoryImpl::instance_,
                                        server.threadLocal(), *config.getObject("conn_pool")));
  std::shared_ptr<Redis::CommandSplitter::Instance> splitter(
      new Redis::CommandSplitter::InstanceImpl(std::move(conn_pool), server.stats(),
                                               filter_config->statPrefix()));
  return [splitter, filter_config](Network::FilterManager& filter_manager) -> void {
    Redis::DecoderFactoryImpl factory;
    filter_manager.addReadFilter(std::make_shared<Redis::ProxyFilter>(
        factory, Redis::EncoderPtr{new Redis::EncoderImpl()}, *splitter, filter_config));
  };
}

std::string RedisProxyFilterConfigFactory::name() { return "redis_proxy"; }

/**
 * Static registration for the redis filter. @see RegisterNamedNetworkFilterConfigFactory.
 */
static RegisterNamedNetworkFilterConfigFactory<RedisProxyFilterConfigFactory> registered_;

} // Configuration
} // Server
} // Envoy
