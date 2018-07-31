#pragma once

#include <map>
#include <string>

#include "envoy/config/filter/network/thrift_proxy/v2alpha1/thrift_proxy.pb.h"
#include "envoy/config/filter/network/thrift_proxy/v2alpha1/thrift_proxy.pb.validate.h"
#include "envoy/stats/stats.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/thrift_proxy/conn_manager.h"
#include "extensions/filters/network/thrift_proxy/filters/filter.h"
#include "extensions/filters/network/thrift_proxy/router/router_impl.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * Config registration for the thrift proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class ThriftProxyFilterConfigFactory
    : public Common::FactoryBase<
          envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy> {
public:
  ThriftProxyFilterConfigFactory() : FactoryBase(NetworkFilterNames::get().ThriftProxy) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

class ConfigImpl : public Config,
                   public Router::Config,
                   public ThriftFilters::FilterChainFactory,
                   Logger::Loggable<Logger::Id::config> {
public:
  ConfigImpl(const envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy& config,
             Server::Configuration::FactoryContext& context);

  // ThriftFilters::FilterChainFactory
  void createFilterChain(ThriftFilters::FilterChainFactoryCallbacks& callbacks) override;

  // Router::Config
  Router::RouteConstSharedPtr route(const std::string& method_name) const override {
    return route_matcher_->route(method_name);
  }

  // Config
  ThriftFilterStats& stats() override { return stats_; }
  ThriftFilters::FilterChainFactory& filterFactory() override { return *this; }
  DecoderPtr createDecoder(DecoderCallbacks& callbacks) override;
  Router::Config& routerConfig() override { return *this; }

private:
  TransportPtr createTransport();
  ProtocolPtr createProtocol();

  Server::Configuration::FactoryContext& context_;
  const std::string stats_prefix_;
  ThriftFilterStats stats_;
  envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy_TransportType transport_;
  envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy_ProtocolType proto_;
  std::unique_ptr<Router::RouteMatcher> route_matcher_;

  std::list<ThriftFilters::FilterFactoryCb> filter_factories_;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
