#pragma once

#include "envoy/http/filter.h"
#include "envoy/network/filter.h"
#include "envoy/server/configuration.h"
#include "envoy/server/instance.h"

#include "common/common/logger.h"
#include "common/json/json_loader.h"
#include "common/stats/stats_scope_impl.h"

namespace Server {
namespace Configuration {

enum class NetworkFilterType { Read, Write, Both };

/**
 * This lambda is used to wrap the creation of a network filter chain for new connections as they
 * come in. Filter factories create the lambda at configuration initialization time, and then they
 * are used at runtime. This maps JSON -> runtime configuration.
 */
typedef std::function<void(Network::FilterManager& filter_manager)> NetworkFilterFactoryCb;

/**
 * Implemented by each network filter and registered via registerNetworkFilterConfigFactory() or
 * the convenience class RegisterNetworkFilterConfigFactory.
 */
class NetworkFilterConfigFactory {
public:
  virtual ~NetworkFilterConfigFactory() {}

  virtual NetworkFilterFactoryCb tryCreateFilterFactory(NetworkFilterType type,
                                                        const std::string& name,
                                                        const Json::Object& config,
                                                        Server::Instance& server) PURE;
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
  static void buildFilterChain(Network::FilterManager& filter_manager,
                               const std::list<NetworkFilterFactoryCb>& factories);
};

/**
 * Implementation of Server::Configuration::Main that reads a configuration from a JSON file.
 */
class MainImpl : Logger::Loggable<Logger::Id::config>, public Main {
public:
  MainImpl(Server::Instance& server);

  static void registerNetworkFilterConfigFactory(NetworkFilterConfigFactory& factory) {
    filterConfigFactories().push_back(&factory);
  }

  /**
   * Initialize the configuration. This happens here vs. the constructor because the initialization
   * will call through the server to get the cluster manager so the server variable must be
   * initialized.
   */
  void initialize(const Json::Object& json);

  // Server::Configuration::Main
  Upstream::ClusterManager& clusterManager() override { return *cluster_manager_; }
  Tracing::HttpTracer& httpTracer() override { return *http_tracer_; }
  const std::list<ListenerPtr>& listeners() override;
  RateLimit::ClientFactory& rateLimitClientFactory() override { return *ratelimit_client_factory_; }
  Optional<std::string> statsdTcpClusterName() override { return statsd_tcp_cluster_name_; }
  Optional<uint32_t> statsdUdpPort() override { return statsd_udp_port_; }
  std::chrono::milliseconds statsFlushInterval() override { return stats_flush_interval_; }

private:
  /**
   * Initialize tracers and corresponding sinks.
   */
  void initializeTracers(const Json::Object& tracing_configuration);

  /**
   * Maps JSON config to runtime config for a listener with network filter chain.
   */
  class ListenerConfig : public Server::Configuration::Listener,
                         public Network::FilterChainFactory {
  public:
    ListenerConfig(MainImpl& parent, Json::Object& json);

    // Server::Configuration::Listener
    Network::FilterChainFactory& filterChainFactory() override { return *this; }
    uint64_t port() override { return port_; }
    Ssl::ServerContext* sslContext() override { return ssl_context_; }
    bool useProxyProto() override { return use_proxy_proto_; }

    // Network::FilterChainFactory
    void createFilterChain(Network::Connection& connection) override;

  private:
    MainImpl& parent_;
    uint64_t port_;
    Stats::ScopeImpl scope_;
    Ssl::ServerContext* ssl_context_{};
    bool use_proxy_proto_{};
    std::list<NetworkFilterFactoryCb> filter_factories_;
  };

  static std::list<NetworkFilterConfigFactory*>& filterConfigFactories() {
    static std::list<NetworkFilterConfigFactory*> filter_config_factories;
    return filter_config_factories;
  }

  Server::Instance& server_;
  std::unique_ptr<Upstream::ClusterManagerFactory> cluster_manager_factory_;
  std::unique_ptr<Upstream::ClusterManager> cluster_manager_;
  Tracing::HttpTracerPtr http_tracer_;
  std::list<Server::Configuration::ListenerPtr> listeners_;
  Optional<std::string> statsd_tcp_cluster_name_;
  Optional<uint32_t> statsd_udp_port_;
  RateLimit::ClientFactoryPtr ratelimit_client_factory_;
  std::chrono::milliseconds stats_flush_interval_;
};

/**
 * @see NetworkFilterConfigFactory.
 */
template <class T> class RegisterNetworkFilterConfigFactory {
public:
  RegisterNetworkFilterConfigFactory() {
    static T instance;
    MainImpl::registerNetworkFilterConfigFactory(instance);
  }
};

/**
 * Initial configuration that reads from JSON.
 */
class InitialImpl : public Initial {
public:
  InitialImpl(const Json::Object& json);

  // Server::Configuration::Initial
  Admin& admin() { return admin_; }
  Optional<std::string> flagsPath() { return flags_path_; }
  Runtime* runtime() override { return runtime_.get(); }

private:
  struct AdminImpl : public Admin {
    // Server::Configuration::Initial::Admin
    const std::string& accessLogPath() { return access_log_path_; }
    uint32_t port() override { return port_; }

    std::string access_log_path_;
    uint32_t port_;
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
