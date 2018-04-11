#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/router/route_config_provider_manager.h"
#include "envoy/server/filter_config.h"

#include "common/common/logger.h"
#include "common/config/well_known_names.h"
#include "common/http/conn_manager_impl.h"
#include "common/json/json_loader.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {

/**
 * Config registration for the HTTP connection manager filter. @see NamedNetworkFilterConfigFactory.
 */
class HttpConnectionManagerFilterConfigFactory
    : Logger::Loggable<Logger::Id::config>,
      public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  Server::Configuration::NetworkFilterFactoryCb
  createFilterFactory(const Json::Object& json_config,
                      Server::Configuration::FactoryContext& context) override;
  Server::Configuration::NetworkFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::unique_ptr<
        envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager>(
        new envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager());
  }
  std::string name() override { return Config::NetworkFilterNames::get().HTTP_CONNECTION_MANAGER; }

private:
  Server::Configuration::NetworkFilterFactoryCb createFilter(
      const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
          proto_config,
      Server::Configuration::FactoryContext& context);
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

  // Http::ConnectionManagerConfig
  const std::list<AccessLog::InstanceSharedPtr>& accessLogs() override { return access_logs_; }
  Http::ServerConnectionPtr createCodec(Network::Connection& connection,
                                        const Buffer::Instance& data,
                                        Http::ServerConnectionCallbacks& callbacks) override;
  Http::DateProvider& dateProvider() override { return date_provider_; }
  std::chrono::milliseconds drainTimeout() override { return drain_timeout_; }
  FilterChainFactory& filterFactory() override { return *this; }
  bool generateRequestId() override { return generate_request_id_; }
  const absl::optional<std::chrono::milliseconds>& idleTimeout() override { return idle_timeout_; }
  Router::RouteConfigProvider& routeConfigProvider() override { return *route_config_provider_; }
  const std::string& serverName() override { return server_name_; }
  Http::ConnectionManagerStats& stats() override { return stats_; }
  Http::ConnectionManagerTracingStats& tracingStats() override { return tracing_stats_; }
  bool useRemoteAddress() override { return use_remote_address_; }
  uint32_t xffNumTrustedHops() const override { return xff_num_trusted_hops_; }
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
  enum class CodecType { HTTP1, HTTP2, AUTO };

  Server::Configuration::FactoryContext& context_;
  std::list<Server::Configuration::HttpFilterFactoryCb> filter_factories_;
  std::list<AccessLog::InstanceSharedPtr> access_logs_;
  const std::string stats_prefix_;
  Http::ConnectionManagerStats stats_;
  Http::ConnectionManagerTracingStats tracing_stats_;
  const bool use_remote_address_{};
  const uint32_t xff_num_trusted_hops_;
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
  Router::RouteConfigProviderSharedPtr route_config_provider_;
  std::chrono::milliseconds drain_timeout_;
  bool generate_request_id_;
  Http::DateProvider& date_provider_;
  Http::ConnectionManagerListenerStats listener_stats_;
  const bool proxy_100_continue_;
};

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
