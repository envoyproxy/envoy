#pragma once

#include <map>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/thrift_proxy/conn_manager.h"
#include "source/extensions/filters/network/thrift_proxy/filters/filter.h"
#include "source/extensions/filters/network/thrift_proxy/protocol_options_config.h"
#include "source/extensions/filters/network/thrift_proxy/router/rds.h"
#include "source/extensions/filters/network/thrift_proxy/router/router_impl.h"
#include "source/extensions/filters/network/well_known_names.h"

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

  absl::StatusOr<Upstream::ProtocolOptionsConfigConstSharedPtr> createProtocolOptionsTyped(
      const envoy::extensions::filters::network::thrift_proxy::v3::ThriftProtocolOptions&
          proto_config,
      Server::Configuration::ProtocolOptionsFactoryContext&) override {
    return std::make_shared<ProtocolOptionsConfigImpl>(proto_config);
  }
};

class ConfigImpl : public Config,
                   public Router::Config,
                   public ThriftFilters::FilterChainFactory,
                   Logger::Loggable<Logger::Id::config> {
public:
  ConfigImpl(const envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy& config,
             Server::Configuration::FactoryContext& context,
             Router::RouteConfigProviderManager& route_config_provider_manager);

  // ThriftFilters::FilterChainFactory
  void createFilterChain(ThriftFilters::FilterChainFactoryCallbacks& callbacks) override;

  // Router::Config
  Router::RouteConstSharedPtr route(const MessageMetadata& metadata,
                                    uint64_t random_value) const override {
    auto config = std::static_pointer_cast<const Router::Config>(route_config_provider_->config());
    return config->route(metadata, random_value);
  }

  // Config
  ThriftFilterStats& stats() override { return stats_; }
  ThriftFilters::FilterChainFactory& filterFactory() override { return *this; }
  TransportPtr createTransport() override;
  ProtocolPtr createProtocol() override;
  Router::Config& routerConfig() override { return *this; }
  bool payloadPassthrough() const override { return payload_passthrough_; }
  uint64_t maxRequestsPerConnection() const override { return max_requests_per_connection_; }
  const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const override {
    return access_logs_;
  }
  bool headerKeysPreserveCase() const override { return header_keys_preserve_case_; }

private:
  void processFilter(
      const envoy::extensions::filters::network::thrift_proxy::v3::ThriftFilter& proto_config);

  Server::Configuration::FactoryContext& context_;
  const std::string stats_prefix_;
  ThriftFilterStats stats_;
  const TransportType transport_;
  const ProtocolType proto_;
  Rds::RouteConfigProviderSharedPtr route_config_provider_;

  std::list<ThriftFilters::FilterFactoryCb> filter_factories_;
  const bool payload_passthrough_;

  const uint64_t max_requests_per_connection_{};
  std::vector<AccessLog::InstanceSharedPtr> access_logs_;
  const bool header_keys_preserve_case_;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
