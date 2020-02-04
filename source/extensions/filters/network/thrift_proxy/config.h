#pragma once

#include <map>
#include <string>

#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.validate.h"

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
 * Provides Thrift-specific cluster options.
 */
class ProtocolOptionsConfigImpl : public ProtocolOptionsConfig {
public:
  ProtocolOptionsConfigImpl(
      const envoy::extensions::filters::network::thrift_proxy::v3::ThriftProtocolOptions&
          proto_config);

  // ProtocolOptionsConfig
  TransportType transport(TransportType downstream_transport) const override;
  ProtocolType protocol(ProtocolType downstream_protocol) const override;

private:
  const TransportType transport_;
  const ProtocolType protocol_;
};

/**
 * Config registration for the thrift proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class ThriftProxyFilterConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy,
          envoy::extensions::filters::network::thrift_proxy::v3::ThriftProtocolOptions> {
public:
  ThriftProxyFilterConfigFactory() : FactoryBase(NetworkFilterNames::get().ThriftProxy, true) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;

  Upstream::ProtocolOptionsConfigConstSharedPtr createProtocolOptionsTyped(
      const envoy::extensions::filters::network::thrift_proxy::v3::ThriftProtocolOptions&
          proto_config) override {
    return std::make_shared<ProtocolOptionsConfigImpl>(proto_config);
  }
};

class ConfigImpl : public Config,
                   public Router::Config,
                   public ThriftFilters::FilterChainFactory,
                   Logger::Loggable<Logger::Id::config> {
public:
  ConfigImpl(const envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy& config,
             Server::Configuration::FactoryContext& context);

  // ThriftFilters::FilterChainFactory
  void createFilterChain(ThriftFilters::FilterChainFactoryCallbacks& callbacks) override;

  // Router::Config
  Router::RouteConstSharedPtr route(const MessageMetadata& metadata,
                                    uint64_t random_value) const override {
    return route_matcher_->route(metadata, random_value);
  }

  // Config
  ThriftFilterStats& stats() override { return stats_; }
  ThriftFilters::FilterChainFactory& filterFactory() override { return *this; }
  TransportPtr createTransport() override;
  ProtocolPtr createProtocol() override;
  Router::Config& routerConfig() override { return *this; }

private:
  void processFilter(
      const envoy::extensions::filters::network::thrift_proxy::v3::ThriftFilter& proto_config);

  Server::Configuration::FactoryContext& context_;
  const std::string stats_prefix_;
  ThriftFilterStats stats_;
  const TransportType transport_;
  const ProtocolType proto_;
  std::unique_ptr<Router::RouteMatcher> route_matcher_;

  std::list<ThriftFilters::FilterFactoryCb> filter_factories_;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
