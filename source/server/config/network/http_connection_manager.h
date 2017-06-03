#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/server/instance.h"

#include "common/common/logger.h"
#include "common/http/conn_manager_impl.h"
#include "common/http/date_provider_impl.h"
#include "common/json/json_loader.h"
#include "common/json/json_validator.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

enum class HttpFilterType { Decoder, Encoder, Both };

/**
 * Config registration for the HTTP connection manager filter. @see NamedNetworkFilterConfigFactory.
 */
class HttpConnectionManagerFilterConfigFactory : Logger::Loggable<Logger::Id::config>,
                                                 public NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  NetworkFilterFactoryCb createFilterFactory(NetworkFilterType type, const Json::Object& config,
                                             Server::Instance& server) override;

  std::string name() override;
};

/**
 * Callback lambda used for dynamic HTTP filter chain construction.
 */
typedef std::function<void(Http::FilterChainFactoryCallbacks&)> HttpFilterFactoryCb;

/**
 * DEPRECATED - Implemented by each HTTP filter and registered via registerHttpFilterConfigFactory()
 * or the convenience class RegisterHttpFilterConfigFactory.
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
 * Implemented by each HTTP filter and registered via registerNamedHttpFilterConfigFactory() or the
 * convenience class RegisterNamedHttpFilterConfigFactory.
 */
class NamedHttpFilterConfigFactory {
public:
  virtual ~NamedHttpFilterConfigFactory() {}

  /**
  * Create a particular http filter factory implementation.  If the implementation is unable to
  * produce a factory with the provided parameters, it should throw an EnvoyException in the case of
  * general error or a Json::Exception if the json configuration is erroneous.  The returned
  * callback should always be initialized.
  * @param type supplies type of filter to initialize (decoder, encoder, or both)
  * @param config supplies the general json configuration for the filter
  * @param stat_prefix prefix for stat logging
  * @param server supplies the server instance
  */
  virtual HttpFilterFactoryCb createFilterFactory(HttpFilterType type, const Json::Object& config,
                                                  const std::string& stat_prefix,
                                                  Server::Instance& server) PURE;

  /**
  * Returns the identifying name for a particular implementation of an http filter produced by the
  * factory.
  */
  virtual std::string name() PURE;
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
                                    public Http::ConnectionManagerConfig,
                                    Json::Validator {
public:
  HttpConnectionManagerConfig(const Json::Object& config, Server::Instance& server);

  // Http::FilterChainFactory
  void createFilterChain(Http::FilterChainFactoryCallbacks& callbacks) override;

  // Http::ConnectionManagerConfig
  const std::list<Http::AccessLog::InstanceSharedPtr>& accessLogs() override {
    return access_logs_;
  }
  Http::ServerConnectionPtr createCodec(Network::Connection& connection,
                                        const Buffer::Instance& data,
                                        Http::ServerConnectionCallbacks& callbacks) override;
  Http::DateProvider& dateProvider() override { return date_provider_; }
  std::chrono::milliseconds drainTimeout() override { return drain_timeout_; }
  FilterChainFactory& filterFactory() override { return *this; }
  bool generateRequestId() override { return generate_request_id_; }
  const Optional<std::chrono::milliseconds>& idleTimeout() override { return idle_timeout_; }
  Router::RouteConfigProvider& routeConfigProvider() override { return *route_config_provider_; }
  const std::string& serverName() override { return server_name_; }
  Http::ConnectionManagerStats& stats() override { return stats_; }
  Http::ConnectionManagerTracingStats& tracingStats() override { return tracing_stats_; }
  bool useRemoteAddress() override { return use_remote_address_; }
  Http::ForwardClientCertType forwardClientCert() override { return forward_client_cert_; }
  const std::list<Http::ClientCertDetailsType> setClientCertDetails() override { return set_client_cert_details_; }
  const Http::TracingConnectionManagerConfig* tracingConfig() override {
    return tracing_config_.get();
  }
  const Network::Address::Instance& localAddress() override;
  const Optional<std::string>& userAgent() override { return user_agent_; }

  /**
   * DEPRECATED - Register an HttpFilterConfigFactory implementation as an option to create
   * instances of HttpFilterFactoryCb.
   * @param factory the HttpFilterConfigFactory implementation
   */
  static void registerHttpFilterConfigFactory(HttpFilterConfigFactory& factory) {
    filterConfigFactories().push_back(&factory);
  }

  /**
   * Register a NamedHttpFilterConfigFactory implementation as an option to create instances of
   * HttpFilterFactoryCb.
   * @param factory the NamedHttpFilterConfigFactory implementation
   */
  static void registerNamedHttpFilterConfigFactory(NamedHttpFilterConfigFactory& factory) {
    auto result = namedFilterConfigFactories().emplace(std::make_pair(factory.name(), &factory));

    // result is a pair whose second member is a boolean indicating, if false, that the key exists
    // and that the value was not inserted.
    if (!result.second) {
      throw EnvoyException(fmt::format(
          "Attempted to register multiple NamedHttpFilterConfigFactory objects with name: '{}'",
          factory.name()));
    }
  }

  static const std::string DEFAULT_SERVER_STRING;

private:
  enum class CodecType { HTTP1, HTTP2, AUTO };

  /**
   * DEPRECATED - Returns a list of the currently registered HttpFilterConfigFactory
   * implementations.
   */
  static std::list<HttpFilterConfigFactory*>& filterConfigFactories() {
    static std::list<HttpFilterConfigFactory*>* filter_config_factories =
        new std::list<HttpFilterConfigFactory*>;
    return *filter_config_factories;
  }

  /**
   * Returns a map of the currently registered NamedHttpFilterConfigFactory implementations.
   */
  static std::unordered_map<std::string, NamedHttpFilterConfigFactory*>&
  namedFilterConfigFactories() {
    static std::unordered_map<std::string, NamedHttpFilterConfigFactory*>*
        named_filter_config_factories =
            new std::unordered_map<std::string, NamedHttpFilterConfigFactory*>;
    return *named_filter_config_factories;
  }

  HttpFilterType stringToType(const std::string& type);

  Server::Instance& server_;
  std::list<HttpFilterFactoryCb> filter_factories_;
  std::list<Http::AccessLog::InstanceSharedPtr> access_logs_;
  const std::string stats_prefix_;
  Http::ConnectionManagerStats stats_;
  Http::ConnectionManagerTracingStats tracing_stats_;
  bool use_remote_address_{};
  Http::ForwardClientCertType forward_client_cert_;
  std::list<Http::ClientCertDetailsType> set_client_cert_details_;
  CodecType codec_type_;
  const Http::Http2Settings http2_settings_;
  std::string server_name_;
  Http::TracingConnectionManagerConfigPtr tracing_config_;
  Optional<std::string> user_agent_;
  Optional<std::chrono::milliseconds> idle_timeout_;
  Router::RouteConfigProviderPtr route_config_provider_;
  std::chrono::milliseconds drain_timeout_;
  bool generate_request_id_;
  Http::TlsCachingDateProviderImpl date_provider_;
};

/**
 * @see NamedHttpFilterConfigFactory. An instantiation of this class registers a
 * NamedHttpFilterConfigFactory implementation (T) so it can be used to create instances of
 * HttpFilterFactoryCb.
 */
template <class T> class RegisterNamedHttpFilterConfigFactory {
public:
  /**
   * Registers the implementation.
   */
  RegisterNamedHttpFilterConfigFactory() {
    static T* instance = new T;
    HttpConnectionManagerConfig::registerNamedHttpFilterConfigFactory(*instance);
  }
};

/**
 * DEPRECATED @see HttpFilterConfigFactory.  An instantiation of this class registers a
 * HttpFilterConfigFactory implementation (T) so it can be used to create instances of
 * HttpFilterFactoryCb.
 */
template <class T> class RegisterHttpFilterConfigFactory {
public:
  /**
   * Registers the implementation.
   */
  RegisterHttpFilterConfigFactory() {
    static T* instance = new T;
    HttpConnectionManagerConfig::registerHttpFilterConfigFactory(*instance);
  }
};

} // Configuration
} // Server
} // Envoy
