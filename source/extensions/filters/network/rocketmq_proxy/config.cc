#include "extensions/filters/network/rocketmq_proxy/config.h"

#include <cstdlib>

#include "envoy/extensions/filters/network/rocketmq_proxy/v3/rocketmq_proxy.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/network/rocketmq_proxy/conn_manager.h"
#include "extensions/filters/network/rocketmq_proxy/stats.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

namespace rocketmq_config = envoy::extensions::filters::network::rocketmq_proxy::v3;

Network::FilterFactoryCb RocketmqProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const rocketmq_config::RocketmqProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  std::shared_ptr<ConfigImpl> filter_config = std::make_shared<ConfigImpl>(proto_config, context);
  return [filter_config, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        std::make_shared<ConnectionManager>(*filter_config, context.dispatcher().timeSource()));
  };
}

REGISTER_FACTORY(RocketmqProxyFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

ConfigImpl::ConfigImpl(const RocketmqProxyConfig& config,
                       Server::Configuration::FactoryContext& context)
    : context_(context), stats_prefix_(fmt::format("rocketmq.{}.", config.stat_prefix())),
      stats_(RocketmqFilterStats::generateStats(stats_prefix_, context_.scope())),
      route_matcher_(new Router::RouteMatcher(config.route_config())),
      develop_mode_(config.develop_mode()),
      transient_object_life_span_(PROTOBUF_GET_MS_OR_DEFAULT(config, transient_object_life_span,
                                                             TransientObjectLifeSpan)) {}

std::string ConfigImpl::proxyAddress() {
  const LocalInfo::LocalInfo& localInfo = context_.getServerFactoryContext().localInfo();
  Network::Address::InstanceConstSharedPtr address = localInfo.address();
  if (address->type() == Network::Address::Type::Ip) {
    const std::string& ip = address->ip()->addressAsString();
    std::string proxyAddr{ip};
    if (address->ip()->port()) {
      return proxyAddr.append(":").append(std::to_string(address->ip()->port()));
    } else {
      ENVOY_LOG(trace, "Local info does not have port specified, defaulting to 10000");
      return proxyAddr.append(":10000");
    }
  }
  return address->asString();
}

Router::RouteConstSharedPtr ConfigImpl::route(const MessageMetadata& metadata) const {
  return route_matcher_->route(metadata);
}

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy