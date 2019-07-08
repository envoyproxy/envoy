#pragma once

#include <string>

#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/dubbo_proxy.pb.h"
#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/dubbo_proxy.pb.validate.h"

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
    : public Common::FactoryBase<
          envoy::config::filter::network::dubbo_proxy::v2alpha1::DubboProxy> {
public:
  DubboProxyFilterConfigFactory() : FactoryBase(NetworkFilterNames::get().DubboProxy) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::dubbo_proxy::v2alpha1::DubboProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

class ConfigImpl : public Config,
                   public Router::Config,
                   public DubboFilters::FilterChainFactory,
                   Logger::Loggable<Logger::Id::config> {
public:
  using DubboProxyConfig = envoy::config::filter::network::dubbo_proxy::v2alpha1::DubboProxy;
  using DubboFilterConfig = envoy::config::filter::network::dubbo_proxy::v2alpha1::DubboFilter;

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
  DeserializerPtr createDeserializer() override;

private:
  void registerFilter(const DubboFilterConfig& proto_config);

  Server::Configuration::FactoryContext& context_;
  const std::string stats_prefix_;
  DubboFilterStats stats_;
  const SerializationType serialization_type_;
  const ProtocolType protocol_type_;
  std::unique_ptr<Router::MultiRouteMatcher> route_matcher_;

  std::list<DubboFilters::FilterFactoryCb> filter_factories_;
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
