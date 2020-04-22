#pragma once

#include <string>

#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/dubbo_proxy/conn_manager.h"
#include "extensions/filters/network/dubbo_proxy/filters/filter.h"
#include "extensions/filters/network/dubbo_proxy/router/route_matcher.h"
#include "extensions/filters/network/dubbo_proxy/router/router_impl.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

/**
 * Config registration for the dubbo proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class DubboProxyFilterConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy> {
public:
  DubboProxyFilterConfigFactory() : FactoryBase(NetworkFilterNames::get().DubboProxy, true) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

class ConfigImpl : public Config,
                   public Router::Config,
                   public DubboFilters::FilterChainFactory,
                   Logger::Loggable<Logger::Id::config> {
public:
  using DubboProxyConfig = envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy;
  using DubboFilterConfig = envoy::extensions::filters::network::dubbo_proxy::v3::DubboFilter;

  ConfigImpl(const DubboProxyConfig& config, Server::Configuration::FactoryContext& context);
  ~ConfigImpl() override = default;

  // DubboFilters::FilterChainFactory
  void createFilterChain(DubboFilters::FilterChainFactoryCallbacks& callbacks) override;

  // Router::Config
  Router::RouteConstSharedPtr route(const MessageMetadata& metadata,
                                    uint64_t random_value) const override;

  // Config
  DubboFilterStats& stats() override { return stats_; }
  DubboFilters::FilterChainFactory& filterFactory() override { return *this; }
  Router::Config& routerConfig() override { return *this; }
  ProtocolPtr createProtocol() override;

private:
  void registerFilter(const DubboFilterConfig& proto_config);

  Server::Configuration::FactoryContext& context_;
  const std::string stats_prefix_;
  DubboFilterStats stats_;
  const SerializationType serialization_type_;
  const ProtocolType protocol_type_;
  Router::RouteMatcherPtr route_matcher_;

  std::list<DubboFilters::FilterFactoryCb> filter_factories_;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
