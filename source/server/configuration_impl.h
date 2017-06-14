#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "envoy/http/filter.h"
#include "envoy/network/filter.h"
#include "envoy/server/configuration.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/instance.h"
#include "envoy/tracing/http_tracer.h"

#include "common/common/logger.h"
#include "common/json/json_loader.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * DEPRECATED - Implemented by each network filter and registered via
 * registerNetworkFilterConfigFactory() or the convenience class RegisterNetworkFilterConfigFactory.
 */
class NetworkFilterConfigFactory {
public:
  virtual ~NetworkFilterConfigFactory() {}

  virtual NetworkFilterFactoryCb tryCreateFilterFactory(NetworkFilterType type,
                                                        const std::string& name,
                                                        const Json::Object& config,
                                                        Instance& server) PURE;
};

/**
 * Implemented by each HttpTracer and registered via registerHttpTracerFactory() or
 * the convenience class RegisterHttpTracerFactory.
 */
class HttpTracerFactory {
public:
  virtual ~HttpTracerFactory() {}

  /**
  * Create a particular HttpTracer implementation.  If the implementation is unable to produce an
  * HttpTracer with the provided parameters, it should throw an EnvoyException in the case of
  * general error or a Json::Exception if the json configuration is erroneous.  The returned pointer
  * should always be valid.
  * @param json_config supplies the general json configuration for the HttpTracer
  * @param server supplies the server instance
  * @param cluster_manager supplies the cluster_manager instance
  */
  virtual Tracing::HttpTracerPtr createHttpTracer(const Json::Object& json_config, Instance& server,
                                                  Upstream::ClusterManager& cluster_manager) PURE;

  /**
  * Returns the identifying name for a particular implementation of HttpTracer produced by the
  * factory.
  */
  virtual std::string name() PURE;
};

/**
 * Utilities for creating a filter chain for a network connection.
 */
class FilterChainUtility {
public:
  /**
   * Given a connection and a list of factories, create a new filter chain. Chain creation will
   * exit early if any filters immediately close the connection.
   */
  static bool buildFilterChain(Network::FilterManager& filter_manager,
                               const std::list<NetworkFilterFactoryCb>& factories);
};

/**
 * Implementation of Server::Configuration::Main that reads a configuration from a JSON file.
 */
class MainImpl : Logger::Loggable<Logger::Id::config>, public Main {
public:
  /**
   * DEPRECATED - Register an NetworkFilterConfigFactory implementation as an option to create
   * instances of NetworkFilterFactoryCb.
   * @param factory the NetworkFilterConfigFactory implementation
   */
  static void registerNetworkFilterConfigFactory(NetworkFilterConfigFactory& factory) {
    filterConfigFactories_().push_back(&factory);
  }

  /**
   * Register an NamedNetworkFilterConfigFactory implementation as an option to create instances of
   * NetworkFilterFactoryCb.
   * @param factory the NamedNetworkFilterConfigFactory implementation
   */
  static void registerNamedNetworkFilterConfigFactory(NamedNetworkFilterConfigFactory& factory) {
    auto result = namedFilterConfigFactories_().emplace(std::make_pair(factory.name(), &factory));

    // result is a pair whose second member is a boolean indicating, if false, that the key exists
    // and that the value was not inserted.
    if (!result.second) {
      throw EnvoyException(fmt::format(
          "Attempted to register multiple NamedNetworkFilterConfigFactory objects with name: '{}'",
          factory.name()));
    }
  }

  /**
   * Register an HttpTracerFactory as an option to create instances of HttpTracers.
   * @param factory the HttpTracerFactory implementation
   */
  static void registerHttpTracerFactory(HttpTracerFactory& factory) {
    auto result = httpTracerFactories().emplace(std::make_pair(factory.name(), &factory));

    // result is a pair whose second member is a boolean indicating, if false, that the key exists
    // and that the value was not inserted.
    if (!result.second) {
      throw EnvoyException(
          fmt::format("Attempted to register multiple HttpTracerFactory objects with name: '{}'",
                      factory.name()));
    }
  }

  /**
   * @return a list of the currently registered NetworkConfigFactories. DEPRECATED.
   */
  static std::list<NetworkFilterConfigFactory*>& filterConfigFactories() {
    return filterConfigFactories_();
  }

  /**
   * @return a map of the currently registered NamedNetworkConfigFactories.
   */
  static const std::unordered_map<std::string, NamedNetworkFilterConfigFactory*>&
  namedFilterConfigFactories() {
    return namedFilterConfigFactories_();
  }

  /**
   * Initialize the configuration. This happens here vs. the constructor because the initialization
   * will call through the server into the config to get the cluster manager so the config object
   * must be created already.
   * @param json supplies the configuration JSON.
   * @param server supplies the owning server.
   * @param cluster_manager_factory supplies the cluster manager creation factory.
   */
  void initialize(const Json::Object& json, Instance& server,
                  Upstream::ClusterManagerFactory& cluster_manager_factory);

  // Server::Configuration::Main
  Upstream::ClusterManager& clusterManager() override { return *cluster_manager_; }
  Tracing::HttpTracer& httpTracer() override { return *http_tracer_; }
  RateLimit::ClientFactory& rateLimitClientFactory() override { return *ratelimit_client_factory_; }
  Optional<std::string> statsdTcpClusterName() override { return statsd_tcp_cluster_name_; }
  // TODO(hennna): DEPRECATED - statsdUdpPort() will be removed in 1.4.0
  Optional<uint32_t> statsdUdpPort() override { return statsd_udp_port_; }
  Optional<std::string> statsdUdpIpAddress() override { return statsd_udp_ip_address_; }
  std::chrono::milliseconds statsFlushInterval() override { return stats_flush_interval_; }
  std::chrono::milliseconds wdMissTimeout() const override { return watchdog_miss_timeout_; }
  std::chrono::milliseconds wdMegaMissTimeout() const override {
    return watchdog_megamiss_timeout_;
  }
  std::chrono::milliseconds wdKillTimeout() const override { return watchdog_kill_timeout_; }
  std::chrono::milliseconds wdMultiKillTimeout() const override {
    return watchdog_multikill_timeout_;
  }

private:
  /**
   * Initialize tracers and corresponding sinks.
   */
  void initializeTracers(const Json::Object& tracing_configuration, Instance& server);

  /**
   * DEPRECATED - Returns a list of the currently registered NetworkConfigFactories.
   */
  static std::list<NetworkFilterConfigFactory*>& filterConfigFactories_() {
    static std::list<NetworkFilterConfigFactory*>* filter_config_factories =
        new std::list<NetworkFilterConfigFactory*>;
    return *filter_config_factories;
  }

  /**
   * Returns a map of the currently registered NamedNetworkConfigFactories.
   */
  static std::unordered_map<std::string, NamedNetworkFilterConfigFactory*>&
  namedFilterConfigFactories_() {
    static std::unordered_map<std::string, NamedNetworkFilterConfigFactory*>*
        named_filter_config_factories =
            new std::unordered_map<std::string, NamedNetworkFilterConfigFactory*>;
    return *named_filter_config_factories;
  }

  /**
   * Returns a map of the currently registered HttpTracerFactories.
   */
  static std::unordered_map<std::string, HttpTracerFactory*>& httpTracerFactories() {
    static std::unordered_map<std::string, HttpTracerFactory*>* http_tracer_factories =
        new std::unordered_map<std::string, HttpTracerFactory*>;
    return *http_tracer_factories;
  }

  std::unique_ptr<Upstream::ClusterManager> cluster_manager_;
  Tracing::HttpTracerPtr http_tracer_;
  Optional<std::string> statsd_tcp_cluster_name_;
  Optional<uint32_t> statsd_udp_port_;
  Optional<std::string> statsd_udp_ip_address_;
  RateLimit::ClientFactoryPtr ratelimit_client_factory_;
  std::chrono::milliseconds stats_flush_interval_;
  std::chrono::milliseconds watchdog_miss_timeout_;
  std::chrono::milliseconds watchdog_megamiss_timeout_;
  std::chrono::milliseconds watchdog_kill_timeout_;
  std::chrono::milliseconds watchdog_multikill_timeout_;
};

/**
 * DEPRECATED - @see NetworkFilterConfigFactory.  An instantiation of this class registers a
 * NetworkFilterConfigFactory implementation (T) so it can be used to create instances of
 * NetworkFilterFactoryCb.
 */
template <class T> class RegisterNetworkFilterConfigFactory {
public:
  /**
   * Registers the implementation.
   */
  RegisterNetworkFilterConfigFactory() {
    static T* instance = new T;
    MainImpl::registerNetworkFilterConfigFactory(*instance);
  }
};

/**
 * @see NamedNetworkFilterConfigFactory.  An instantiation of this class registers a
 * NamedNetworkFilterConfigFactory implementation (T) so it can be used to create instances of
 * NetworkFilterFactoryCb.
 */
template <class T> class RegisterNamedNetworkFilterConfigFactory {
public:
  /**
   * Registers the implementation.
   */
  RegisterNamedNetworkFilterConfigFactory() {
    static T* instance = new T;
    MainImpl::registerNamedNetworkFilterConfigFactory(*instance);
  }
};

/**
 * @see HttpTracerFactory.  An instantiation of this class registers an HttpTracerFactory
 * implementation (T) so it can be used to create instances of HttpTracer.
 */
template <class T> class RegisterHttpTracerFactory {
public:
  /**
   * Registers the implementation.
   */
  RegisterHttpTracerFactory() {
    static T* instance = new T;
    MainImpl::registerHttpTracerFactory(*instance);
  }
};

/**
 * Initial configuration that reads from JSON.
 */
class InitialImpl : public Initial {
public:
  InitialImpl(const Json::Object& json);

  // Server::Configuration::Initial
  Admin& admin() override { return admin_; }
  Optional<std::string> flagsPath() override { return flags_path_; }
  Runtime* runtime() override { return runtime_.get(); }

private:
  struct AdminImpl : public Admin {
    // Server::Configuration::Initial::Admin
    const std::string& accessLogPath() override { return access_log_path_; }
    const std::string& profilePath() override { return profile_path_; }
    Network::Address::InstanceConstSharedPtr address() override { return address_; }

    std::string access_log_path_;
    std::string profile_path_;
    Network::Address::InstanceConstSharedPtr address_;
  };

  struct RuntimeImpl : public Runtime {
    // Server::Configuration::Runtime
    const std::string& symlinkRoot() override { return symlink_root_; }
    const std::string& subdirectory() override { return subdirectory_; }
    const std::string& overrideSubdirectory() override { return override_subdirectory_; }

    std::string symlink_root_;
    std::string subdirectory_;
    std::string override_subdirectory_;
  };

  AdminImpl admin_;
  Optional<std::string> flags_path_;
  std::unique_ptr<RuntimeImpl> runtime_;
};

} // Configuration
} // Server
} // Envoy
