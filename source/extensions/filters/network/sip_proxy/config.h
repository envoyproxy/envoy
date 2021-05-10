#pragma once

#include <map>
#include <string>

#include "envoy/extensions/filters/network/sip_proxy/v3/sip_proxy.pb.h"
#include "envoy/extensions/filters/network/sip_proxy/v3/sip_proxy.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/sip_proxy/conn_manager.h"
#include "extensions/filters/network/sip_proxy/filters/filter.h"
#include "extensions/filters/network/sip_proxy/router/router_impl.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

/**
 * Provides Sip-specific cluster options.
 */
class ProtocolOptionsConfigImpl : public ProtocolOptionsConfig {
public:
  ProtocolOptionsConfigImpl(
      const envoy::extensions::filters::network::sip_proxy::v3::SipProtocolOptions&
          proto_config);
};

/**
 * Config registration for the sip proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class SipProxyFilterConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::sip_proxy::v3::SipProxy,
          envoy::extensions::filters::network::sip_proxy::v3::SipProtocolOptions> {
public:
  SipProxyFilterConfigFactory() : FactoryBase(NetworkFilterNames::get().SipProxy, true) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::sip_proxy::v3::SipProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;

  Upstream::ProtocolOptionsConfigConstSharedPtr createProtocolOptionsTyped(
      const envoy::extensions::filters::network::sip_proxy::v3::SipProtocolOptions&
          proto_config,
      Server::Configuration::ProtocolOptionsFactoryContext&) override {
    return std::make_shared<ProtocolOptionsConfigImpl>(proto_config);
  }
};

class ConfigImpl : public Config,
                   public Router::Config,
                   public SipFilters::FilterChainFactory,
                   Logger::Loggable<Logger::Id::config> {
public:
  ConfigImpl(const envoy::extensions::filters::network::sip_proxy::v3::SipProxy& config,
             Server::Configuration::FactoryContext& context);

  // SipFilters::FilterChainFactory
  void createFilterChain(SipFilters::FilterChainFactoryCallbacks& callbacks) override;

  // Router::Config
  Router::RouteConstSharedPtr route(MessageMetadata& metadata,
                                    uint64_t random_value) const override {
    return route_matcher_->route(metadata, random_value);
  }

  // Config
  SipFilterStats& stats() override { return stats_; }
  SipFilters::FilterChainFactory& filterFactory() override { return *this; }
  Router::Config& routerConfig() override { return *this; }
  std::shared_ptr<SipSettings> settings() override { return settings_; }

  // Settings
private:
  void processFilter(
      const envoy::extensions::filters::network::sip_proxy::v3::SipFilter& proto_config);

  Server::Configuration::FactoryContext& context_;
  const std::string stats_prefix_;
  SipFilterStats stats_;
  std::unique_ptr<Router::RouteMatcher> route_matcher_;

  std::list<SipFilters::FilterFactoryCb> filter_factories_;

  std::shared_ptr<SipSettings> settings_;
};

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
