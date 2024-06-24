#pragma once

#include "source/extensions/filters/network/common/factory_base.h"

#include "contrib/envoy/extensions/filters/network/sip_proxy/v3alpha/sip_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/sip_proxy/v3alpha/sip_proxy.pb.validate.h"
#include "contrib/sip_proxy/filters/network/source/conn_manager.h"
#include "contrib/sip_proxy/filters/network/source/filters/filter.h"
#include "contrib/sip_proxy/filters/network/source/filters/well_known_names.h"
#include "contrib/sip_proxy/filters/network/source/router/router_impl.h"

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
      const envoy::extensions::filters::network::sip_proxy::v3alpha::SipProtocolOptions&
          proto_config);

  bool sessionAffinity() const override;
  bool registrationAffinity() const override;
  const envoy::extensions::filters::network::sip_proxy::v3alpha::CustomizedAffinity&
  customizedAffinity() const override;

private:
  bool session_affinity_;
  bool registration_affinity_;
  const envoy::extensions::filters::network::sip_proxy::v3alpha::CustomizedAffinity
      customized_affinity_;
};

/**
 * Config registration for the sip proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class SipProxyFilterConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy,
          envoy::extensions::filters::network::sip_proxy::v3alpha::SipProtocolOptions> {
public:
  SipProxyFilterConfigFactory() : FactoryBase(SipFilters::SipFilterNames::get().SipProxy, true) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
  absl::StatusOr<Upstream::ProtocolOptionsConfigConstSharedPtr> createProtocolOptionsTyped(
      const envoy::extensions::filters::network::sip_proxy::v3alpha::SipProtocolOptions&
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
  ConfigImpl(const envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy& config,
             Server::Configuration::FactoryContext& context);

  // SipFilters::FilterChainFactory
  void createFilterChain(SipFilters::FilterChainFactoryCallbacks& callbacks) override;

  // Router::Config
  Router::RouteConstSharedPtr route(MessageMetadata& metadata) const override {
    return route_matcher_->route(metadata);
  }

  // Config
  SipFilterStats& stats() override { return stats_; }
  SipFilters::FilterChainFactory& filterFactory() override { return *this; }
  Router::Config& routerConfig() override { return *this; }
  std::shared_ptr<SipSettings> settings() override { return settings_; }

  // Settings
private:
  void processFilter(
      const envoy::extensions::filters::network::sip_proxy::v3alpha::SipFilter& proto_config);

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
