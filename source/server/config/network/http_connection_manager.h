#pragma once

#include "envoy/http/filter.h"
#include "envoy/server/instance.h"

#include "common/common/logger.h"
#include "common/http/conn_manager_impl.h"
#include "common/json/json_loader.h"

namespace Server {
namespace Configuration {

enum class HttpFilterType { Decoder, Encoder, Both };

/**
 * Callback lambda used for dynamic HTTP filter chain construction.
 */
typedef std::function<void(Http::FilterChainFactoryCallbacks&)> HttpFilterFactoryCb;

/**
 * Implemented by each HTTP filter and registered via registerHttpFilterConfigFactory() or the
 * convenience class RegisterHttpFilterConfigFactory.
 */
class HttpFilterConfigFactory {
public:
  virtual ~HttpFilterConfigFactory() {}

  virtual HttpFilterFactoryCb tryCreateFilterFactory(HttpFilterType type, const std::string& name,
                                                     const Json::Object& config,
                                                     const std::string& stat_prefix,
                                                     Server::Instance& server) PURE;
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
 * Maps JSON config to runtime config for an HTTP connection manager network filter.
 */
class HttpConnectionManagerConfig : Logger::Loggable<Logger::Id::config>,
                                    public Http::FilterChainFactory,
                                    public Http::ConnectionManagerConfig {
public:
  HttpConnectionManagerConfig(const Json::Object& config, Server::Instance& server);

  // Http::FilterChainFactory
  void createFilterChain(Http::FilterChainFactoryCallbacks& callbacks) override;

  // Http::ConnectionManagerConfig
  const std::list<Http::AccessLog::InstancePtr>& accessLogs() override { return access_logs_; }
  Http::ServerConnectionPtr createCodec(Network::Connection& connection,
                                        const Buffer::Instance& data,
                                        Http::ServerConnectionCallbacks& callbacks) override;
  std::chrono::milliseconds drainTimeout() override { return drain_timeout_; }
  FilterChainFactory& filterFactory() override { return *this; }
  bool generateRequestId() override { return generate_request_id_; }
  const Optional<std::chrono::milliseconds>& idleTimeout() override { return idle_timeout_; }
  const Router::Config& routeConfig() override { return *route_config_; }
  const std::string& serverName() override { return server_name_; }
  Http::ConnectionManagerStats& stats() override { return stats_; }
  bool useRemoteAddress() override { return use_remote_address_; }
  bool shouldTraceRequest(const Http::AccessLog::RequestInfo& request_info) override;
  const Optional<Tracing::TracingConnectionManagerConfig>& tracingConfig() override {
    return tracing_info_;
  }
  const std::string& localAddress() override;
  const Optional<std::string>& userAgent() override { return user_agent_; }

  static void registerHttpFilterConfigFactory(HttpFilterConfigFactory& factory) {
    filterConfigFactories().push_back(&factory);
  }

  static const std::string DEFAULT_SERVER_STRING;

private:
  enum class CodecType { HTTP1, HTTP2, AUTO };

  static std::list<HttpFilterConfigFactory*>& filterConfigFactories() {
    static std::list<HttpFilterConfigFactory*> filter_config_factories;
    return filter_config_factories;
  }

  HttpFilterType stringToType(const std::string& type);

  Server::Instance& server_;
  std::list<HttpFilterFactoryCb> filter_factories_;
  std::list<Http::AccessLog::InstancePtr> access_logs_;
  const std::string stats_prefix_;
  Http::ConnectionManagerStats stats_;
  bool use_remote_address_{};
  CodecType codec_type_;
  const uint64_t codec_options_;
  std::string server_name_;
  Optional<Tracing::TracingConnectionManagerConfig> tracing_info_;
  Optional<std::string> user_agent_;
  Optional<std::chrono::milliseconds> idle_timeout_;
  Router::ConfigPtr route_config_;
  std::chrono::milliseconds drain_timeout_;
  bool generate_request_id_;
};

/**
 * @see HttpFilterConfigFactory.
 */
template <class T> class RegisterHttpFilterConfigFactory {
public:
  RegisterHttpFilterConfigFactory() {
    static T instance;
    HttpConnectionManagerConfig::registerHttpFilterConfigFactory(instance);
  }
};

} // Configuration
} // Server
