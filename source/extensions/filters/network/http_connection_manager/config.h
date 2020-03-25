#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <string>

#include "envoy/config/config_provider_manager.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/router/route_config_provider_manager.h"
#include "envoy/tracing/http_tracer_manager.h"

#include "common/common/logger.h"
#include "common/http/conn_manager_impl.h"
#include "common/http/date_provider_impl.h"
#include "common/json/json_loader.h"
#include "common/router/rds_impl.h"
#include "common/router/scoped_rds.h"
#include "common/tracing/http_tracer_impl.h"

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
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager> {
public:
  HttpConnectionManagerFilterConfigFactory()
      : FactoryBase(NetworkFilterNames::get().HttpConnectionManager, true) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
          proto_config,
      Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(HttpConnectionManagerFilterConfigFactory);

/**
 * Determines if an address is internal based on user provided config.
 */
class InternalAddressConfig : public Http::InternalAddressConfig {
public:
  InternalAddressConfig(const envoy::extensions::filters::network::http_connection_manager::v3::
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
      const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
          config,
      Server::Configuration::FactoryContext& context, Http::DateProvider& date_provider,
      Router::RouteConfigProviderManager& route_config_provider_manager,
      Config::ConfigProviderManager& scoped_routes_config_provider_manager,
      Tracing::HttpTracerManager& http_tracer_manager);

  // Http::FilterChainFactory
  void createFilterChain(Http::FilterChainFactoryCallbacks& callbacks) override;
  using FilterFactoriesList = std::list<Http::FilterFactoryCb>;
  struct FilterConfig {
    std::unique_ptr<FilterFactoriesList> filter_factories;
    bool allow_upgrade;
  };
  bool createUpgradeFilterChain(absl::string_view upgrade_type,
                                const Http::FilterChainFactory::UpgradeMap* per_route_upgrade_map,
                                Http::FilterChainFactoryCallbacks& callbacks) override;

  // Http::ConnectionManagerConfig
  const std::list<AccessLog::InstanceSharedPtr>& accessLogs() override { return access_logs_; }
  Http::ServerConnectionPtr createCodec(Network::Connection& connection,
                                        const Buffer::Instance& data,
                                        Http::ServerConnectionCallbacks& callbacks) override;
  Http::DateProvider& dateProvider() override { return date_provider_; }
  std::chrono::milliseconds drainTimeout() const override { return drain_timeout_; }
  FilterChainFactory& filterFactory() override { return *this; }
  bool generateRequestId() const override { return generate_request_id_; }
  bool preserveExternalRequestId() const override { return preserve_external_request_id_; }
  uint32_t maxRequestHeadersKb() const override { return max_request_headers_kb_; }
  uint32_t maxRequestHeadersCount() const override { return max_request_headers_count_; }
  absl::optional<std::chrono::milliseconds> idleTimeout() const override { return idle_timeout_; }
  bool isRoutable() const override { return true; }
  absl::optional<std::chrono::milliseconds> maxConnectionDuration() const override {
    return max_connection_duration_;
  }
  std::chrono::milliseconds streamIdleTimeout() const override { return stream_idle_timeout_; }
  std::chrono::milliseconds requestTimeout() const override { return request_timeout_; }
  absl::optional<std::chrono::milliseconds> maxStreamDuration() const override {
    return max_stream_duration_;
  }
  Router::RouteConfigProvider* routeConfigProvider() override {
    return route_config_provider_.get();
  }
  Config::ConfigProvider* scopedRouteConfigProvider() override {
    return scoped_routes_config_provider_.get();
  }
  const std::string& serverName() const override { return server_name_; }
  HttpConnectionManagerProto::ServerHeaderTransformation
  serverHeaderTransformation() const override {
    return server_transformation_;
  }
  Http::ConnectionManagerStats& stats() override { return stats_; }
  Http::ConnectionManagerTracingStats& tracingStats() override { return tracing_stats_; }
  bool useRemoteAddress() const override { return use_remote_address_; }
  const Http::InternalAddressConfig& internalAddressConfig() const override {
    return *internal_address_config_;
  }
  uint32_t xffNumTrustedHops() const override { return xff_num_trusted_hops_; }
  bool skipXffAppend() const override { return skip_xff_append_; }
  const std::string& via() const override { return via_; }
  Http::ForwardClientCertType forwardClientCert() const override { return forward_client_cert_; }
  const std::vector<Http::ClientCertDetailsType>& setCurrentClientCertDetails() const override {
    return set_current_client_cert_details_;
  }
  Tracing::HttpTracerSharedPtr tracer() override { return http_tracer_; }
  const Http::TracingConnectionManagerConfig* tracingConfig() override {
    return tracing_config_.get();
  }
  const Network::Address::Instance& localAddress() override;
  const absl::optional<std::string>& userAgent() override { return user_agent_; }
  Http::ConnectionManagerListenerStats& listenerStats() override { return listener_stats_; }
  bool proxy100Continue() const override { return proxy_100_continue_; }
  const Http::Http1Settings& http1Settings() const override { return http1_settings_; }
  bool shouldNormalizePath() const override { return normalize_path_; }
  bool shouldMergeSlashes() const override { return merge_slashes_; }
  std::chrono::milliseconds delayedCloseTimeout() const override { return delayed_close_timeout_; }

private:
  enum class CodecType { HTTP1, HTTP2, HTTP3, AUTO };
  void
  processFilter(const envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter&
                    proto_config,
                int i, absl::string_view prefix, FilterFactoriesList& filter_factories,
                const char* filter_chain_type, bool last_filter_in_current_config);

  /**
   * Determines what tracing provider to use for a given
   * "envoy.filters.network.http_connection_manager" filter instance.
   */
  const envoy::config::trace::v3::Tracing_Http* getPerFilterTracerConfig(
      const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
          filter_config);

  Server::Configuration::FactoryContext& context_;
  FilterFactoriesList filter_factories_;
  std::map<std::string, FilterConfig> upgrade_filter_factories_;
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
  Config::ConfigProviderManager& scoped_routes_config_provider_manager_;
  CodecType codec_type_;
  envoy::config::core::v3::Http2ProtocolOptions http2_options_;
  const Http::Http1Settings http1_settings_;
  HttpConnectionManagerProto::ServerHeaderTransformation server_transformation_{
      HttpConnectionManagerProto::OVERWRITE};
  std::string server_name_;
  Tracing::HttpTracerSharedPtr http_tracer_{std::make_shared<Tracing::HttpNullTracer>()};
  Http::TracingConnectionManagerConfigPtr tracing_config_;
  absl::optional<std::string> user_agent_;
  const uint32_t max_request_headers_kb_;
  const uint32_t max_request_headers_count_;
  absl::optional<std::chrono::milliseconds> idle_timeout_;
  absl::optional<std::chrono::milliseconds> max_connection_duration_;
  absl::optional<std::chrono::milliseconds> max_stream_duration_;
  std::chrono::milliseconds stream_idle_timeout_;
  std::chrono::milliseconds request_timeout_;
  Router::RouteConfigProviderSharedPtr route_config_provider_;
  Config::ConfigProviderPtr scoped_routes_config_provider_;
  std::chrono::milliseconds drain_timeout_;
  bool generate_request_id_;
  const bool preserve_external_request_id_;
  Http::DateProvider& date_provider_;
  Http::ConnectionManagerListenerStats listener_stats_;
  const bool proxy_100_continue_;
  std::chrono::milliseconds delayed_close_timeout_;
  const bool normalize_path_;
  const bool merge_slashes_;

  // Default idle timeout is 5 minutes if nothing is specified in the HCM config.
  static const uint64_t StreamIdleTimeoutMs = 5 * 60 * 1000;
  // request timeout is disabled by default
  static const uint64_t RequestTimeoutMs = 0;
};

/**
 * Factory to create an HttpConnectionManager outside of a Network Filter Chain.
 */
class HttpConnectionManagerFactory {
public:
  static std::function<Http::ApiListenerPtr()> createHttpConnectionManagerFactoryFromProto(
      const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
          proto_config,
      Server::Configuration::FactoryContext& context, Network::ReadFilterCallbacks& read_callbacks);
};

/**
 * Utility class for shared logic between HTTP connection manager factories.
 */
class Utility {
public:
  struct Singletons {
    std::shared_ptr<Http::TlsCachingDateProviderImpl> date_provider_;
    std::shared_ptr<Router::RouteConfigProviderManager> route_config_provider_manager_;
    std::shared_ptr<Router::ScopedRoutesConfigProviderManager>
        scoped_routes_config_provider_manager_;
    Tracing::HttpTracerManagerSharedPtr http_tracer_manager_;
  };

  /**
   * Create/get singletons needed for config creation.
   *
   * @param context supplies the context used to create the singletons.
   * @return Singletons struct containing all the singletons.
   */
  static Singletons createSingletons(Server::Configuration::FactoryContext& context);

  /**
   * Create the HttpConnectionManagerConfig.
   *
   * @param proto_config supplies the config to install.
   * @param context supplies the context used to create the config.
   * @param date_provider the singleton used in config creation.
   * @param route_config_provider_manager the singleton used in config creation.
   * @param scoped_routes_config_provider_manager the singleton used in config creation.
   * @return a shared_ptr to the created config object.
   */
  static std::shared_ptr<HttpConnectionManagerConfig> createConfig(
      const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
          proto_config,
      Server::Configuration::FactoryContext& context, Http::DateProvider& date_provider,
      Router::RouteConfigProviderManager& route_config_provider_manager,
      Config::ConfigProviderManager& scoped_routes_config_provider_manager,
      Tracing::HttpTracerManager& http_tracer_manager);
};

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
