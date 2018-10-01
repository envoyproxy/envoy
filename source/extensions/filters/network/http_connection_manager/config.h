#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <string>

#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/router/route_config_provider_manager.h"

#include "common/common/logger.h"
#include "common/http/conn_manager_impl.h"
#include "common/json/json_loader.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {

/**
 * Config registration for the HTTP connection manager filter. @see NamedNetworkFilterConfigFactory.
 */
class HttpConnectionManagerFilterConfigFactory
    : Logger::Loggable<Logger::Id::config>,
      public Common::FactoryBase<
          envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager> {
public:
  HttpConnectionManagerFilterConfigFactory()
      : FactoryBase(NetworkFilterNames::get().HttpConnectionManager) {}

  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config,
                      Server::Configuration::FactoryContext& context) override;

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
          proto_config,
      Server::Configuration::FactoryContext& context) override;
};

/**
 * Utilities for the HTTP connection manager that facilitate testing.
 */
class HttpConnectionManagerConfigUtility {
public:
  /**
   * Determine the next protocol to used based both on ALPN as well as protocol inspection.
   * @param connection supplies the connection to determine a protocol for.
   * @param data supplies the currently available read data on the connection.
   */
  static std::string determineNextProtocol(Network::Connection& connection,
                                           const Buffer::Instance& data);
};

/**
 * Determines if an address is internal based on user provided config.
 */
class InternalAddressConfig : public Http::InternalAddressConfig {
public:
  InternalAddressConfig(const envoy::config::filter::network::http_connection_manager::v2::
                            HttpConnectionManager::InternalAddressConfig& config);

  bool isInternalAddress(const Network::Address::Instance& address) const override {
    if (address.type() == Network::Address::Type::Pipe) {
      return unix_sockets_;
    }

    // TODO(snowp): Make internal subnets configurable.
    return Network::Utility::isInternalAddress(address);
  }

private:
  const bool unix_sockets_;
};

/**
 * Maps proto config to runtime config for an HTTP connection manager network filter.
 */
class HttpConnectionManagerConfig : Logger::Loggable<Logger::Id::config>,
                                    public Http::FilterChainFactory,
                                    public Http::ConnectionManagerConfig {
public:
  HttpConnectionManagerConfig(
      const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
          config,
      Server::Configuration::FactoryContext& context, Http::DateProvider& date_provider,
      Router::RouteConfigProviderManager& route_config_provider_manager);

  // Http::FilterChainFactory
  void createFilterChain(Http::FilterChainFactoryCallbacks& callbacks) override;
  bool createUpgradeFilterChain(absl::string_view upgrade_type,
                                Http::FilterChainFactoryCallbacks& callbacks) override;

  // Http::ConnectionManagerConfig
  const std::list<AccessLog::InstanceSharedPtr>& accessLogs() override { return access_logs_; }
  Http::ServerConnectionPtr createCodec(Network::Connection& connection,
                                        const Buffer::Instance& data,
                                        Http::ServerConnectionCallbacks& callbacks) override;
  Http::DateProvider& dateProvider() override { return date_provider_; }
  std::chrono::milliseconds drainTimeout() override { return drain_timeout_; }
  FilterChainFactory& filterFactory() override { return *this; }
  bool generateRequestId() override { return generate_request_id_; }
  absl::optional<std::chrono::milliseconds> idleTimeout() const override { return idle_timeout_; }
  std::chrono::milliseconds streamIdleTimeout() const override { return stream_idle_timeout_; }
  Router::RouteConfigProvider& routeConfigProvider() override { return *route_config_provider_; }
  const std::string& serverName() override { return server_name_; }
  Http::ConnectionManagerStats& stats() override { return stats_; }
  Http::ConnectionManagerTracingStats& tracingStats() override { return tracing_stats_; }
  bool useRemoteAddress() override { return use_remote_address_; }
  const Http::InternalAddressConfig& internalAddressConfig() const override {
    return *internal_address_config_;
  }
  uint32_t xffNumTrustedHops() const override { return xff_num_trusted_hops_; }
  bool skipXffAppend() const override { return skip_xff_append_; }
  const std::string& via() const override { return via_; }
  Http::ForwardClientCertType forwardClientCert() override { return forward_client_cert_; }
  const std::vector<Http::ClientCertDetailsType>& setCurrentClientCertDetails() const override {
    return set_current_client_cert_details_;
  }
  const Http::TracingConnectionManagerConfig* tracingConfig() override {
    return tracing_config_.get();
  }
  const Network::Address::Instance& localAddress() override;
  const absl::optional<std::string>& userAgent() override { return user_agent_; }
  Http::ConnectionManagerListenerStats& listenerStats() override { return listener_stats_; }
  bool proxy100Continue() const override { return proxy_100_continue_; }
  const Http::Http1Settings& http1Settings() const override { return http1_settings_; }

private:
  typedef std::list<Http::FilterFactoryCb> FilterFactoriesList;
  enum class CodecType { HTTP1, HTTP2, AUTO };
  void processFilter(
      const envoy::config::filter::network::http_connection_manager::v2::HttpFilter& proto_config,
      int i, absl::string_view prefix, FilterFactoriesList& filter_factories);

  Server::Configuration::FactoryContext& context_;
  FilterFactoriesList filter_factories_;
  std::map<std::string, std::unique_ptr<FilterFactoriesList>> upgrade_filter_factories_;
  std::list<AccessLog::InstanceSharedPtr> access_logs_;
  const std::string stats_prefix_;
  Http::ConnectionManagerStats stats_;
  Http::ConnectionManagerTracingStats tracing_stats_;
  const bool use_remote_address_{};
  const std::unique_ptr<Http::InternalAddressConfig> internal_address_config_;
  const uint32_t xff_num_trusted_hops_;
  const bool skip_xff_append_;
  const std::string via_;
  Http::ForwardClientCertType forward_client_cert_;
  std::vector<Http::ClientCertDetailsType> set_current_client_cert_details_;
  Router::RouteConfigProviderManager& route_config_provider_manager_;
  CodecType codec_type_;
  const Http::Http2Settings http2_settings_;
  const Http::Http1Settings http1_settings_;
  std::string server_name_;
  Http::TracingConnectionManagerConfigPtr tracing_config_;
  absl::optional<std::string> user_agent_;
  absl::optional<std::chrono::milliseconds> idle_timeout_;
  std::chrono::milliseconds stream_idle_timeout_;
  Router::RouteConfigProviderPtr route_config_provider_;
  std::chrono::milliseconds drain_timeout_;
  bool generate_request_id_;
  Http::DateProvider& date_provider_;
  Http::ConnectionManagerListenerStats listener_stats_;
  const bool proxy_100_continue_;

  // Default idle timeout is 5 minutes if nothing is specified in the HCM config.
  static const uint64_t StreamIdleTimeoutMs = 5 * 60 * 1000;
};

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
