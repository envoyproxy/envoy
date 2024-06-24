#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <utility>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/trace/v3/http_tracer.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/filter/config_provider_manager.h"
#include "envoy/http/filter.h"
#include "envoy/network/filter.h"
#include "envoy/server/configuration.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/instance.h"

#include "source/common/common/logger.h"
#include "source/common/network/resolver_impl.h"
#include "source/common/network/utility.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Implemented for each Stats::Sink and registered via Registry::registerFactory() or
 * the convenience class RegisterFactory.
 */
class StatsSinkFactory : public Config::TypedFactory {
public:
  ~StatsSinkFactory() override = default;

  /**
   * Create a particular Stats::Sink implementation. If the implementation is unable to produce a
   * Stats::Sink with the provided parameters, it should throw an EnvoyException. The returned
   * pointer should always be valid.
   * @param config supplies the custom proto configuration for the Stats::Sink
   * @param server supplies the server instance
   */
  virtual Stats::SinkPtr createStatsSink(const Protobuf::Message& config,
                                         Server::Configuration::ServerFactoryContext& server) PURE;

  std::string category() const override { return "envoy.stats_sinks"; }
};

class StatsConfigImpl : public StatsConfig {
public:
  StatsConfigImpl(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                  absl::Status& creation_status);

  const std::list<Stats::SinkPtr>& sinks() const override { return sinks_; }
  std::chrono::milliseconds flushInterval() const override { return flush_interval_; }
  bool flushOnAdmin() const override { return flush_on_admin_; }

  void addSink(Stats::SinkPtr sink) { sinks_.emplace_back(std::move(sink)); }
  bool enableDeferredCreationStats() const override {
    return deferred_stat_options_.enable_deferred_creation_stats();
  }

private:
  std::list<Stats::SinkPtr> sinks_;
  std::chrono::milliseconds flush_interval_;
  bool flush_on_admin_{false};
  const envoy::config::bootstrap::v3::Bootstrap::DeferredStatOptions deferred_stat_options_;
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
                               const Filter::NetworkFilterFactoriesList& factories);

  /**
   * Given a ListenerFilterManager and a list of factories, create a new filter chain. Chain
   * creation will exit early if any filters immediately close the connection.
   *
   * TODO(sumukhs): Coalesce with the above as they are very similar
   */
  static bool buildFilterChain(Network::ListenerFilterManager& filter_manager,
                               const Filter::ListenerFilterFactoriesList& factories);

  /**
   * Given a UdpListenerFilterManager and a list of factories, create a new filter chain. Chain
   * creation will exit early if any filters immediately close the connection.
   */
  static void
  buildUdpFilterChain(Network::UdpListenerFilterManager& filter_manager,
                      Network::UdpReadFilterCallbacks& callbacks,
                      const std::vector<Network::UdpListenerFilterFactoryCb>& factories);

  /**
   * Given a QuicListenerFilterManager and a list of factories, create a new filter chain. Chain
   * creation will exit early if any filters don't have a valid config.
   *
   * TODO(sumukhs): Coalesce with the above as they are very similar
   */
  static bool buildQuicFilterChain(Network::QuicListenerFilterManager& filter_manager,
                                   const Filter::QuicListenerFilterFactoriesList& factories);
};

/**
 * Implementation of Server::Configuration::Main that reads a configuration from
 * a JSON file.
 */
class MainImpl : Logger::Loggable<Logger::Id::config>, public Main {
public:
  /**
   * MainImpl is created in two phases. In the first phase it is
   * default-constructed without a configuration as part of the server. The
   * server won't be fully populated yet. initialize() applies the
   * configuration in the second phase, as it requires a fully populated server.
   *
   * @param bootstrap v2 bootstrap proto.
   * @param server supplies the owning server.
   * @param cluster_manager_factory supplies the cluster manager creation factory.
   * @return a status indicating initialization success or failure.
   */
  absl::Status initialize(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                          Instance& server,
                          Upstream::ClusterManagerFactory& cluster_manager_factory);

  // Server::Configuration::Main
  Upstream::ClusterManager* clusterManager() override { return cluster_manager_.get(); }
  const Upstream::ClusterManager* clusterManager() const override { return cluster_manager_.get(); }
  StatsConfig& statsConfig() override { return *stats_config_; }
  const Watchdog& mainThreadWatchdogConfig() const override { return *main_thread_watchdog_; }
  const Watchdog& workerWatchdogConfig() const override { return *worker_watchdog_; }

private:
  /**
   * Initialize tracers and corresponding sinks.
   */
  void initializeTracers(const envoy::config::trace::v3::Tracing& configuration, Instance& server);

  /**
   * Initialize stats configuration.
   */
  void initializeStatsConfig(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                             Instance& server);

  /**
   * Initialize watchdog(s). Call before accessing any watchdog configuration.
   */
  absl::Status initializeWatchdogs(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                                   Instance& server);

  std::unique_ptr<Upstream::ClusterManager> cluster_manager_;
  std::unique_ptr<StatsConfigImpl> stats_config_;
  std::unique_ptr<Watchdog> main_thread_watchdog_;
  std::unique_ptr<Watchdog> worker_watchdog_;
};

class WatchdogImpl : public Watchdog {
public:
  WatchdogImpl(const envoy::config::bootstrap::v3::Watchdog& watchdog, Instance& server);

  std::chrono::milliseconds missTimeout() const override { return miss_timeout_; }
  std::chrono::milliseconds megaMissTimeout() const override { return megamiss_timeout_; }
  std::chrono::milliseconds killTimeout() const override { return kill_timeout_; }
  std::chrono::milliseconds multiKillTimeout() const override { return multikill_timeout_; }
  double multiKillThreshold() const override { return multikill_threshold_; }
  Protobuf::RepeatedPtrField<envoy::config::bootstrap::v3::Watchdog::WatchdogAction>
  actions() const override {
    return actions_;
  }

private:
  std::chrono::milliseconds miss_timeout_;
  std::chrono::milliseconds megamiss_timeout_;
  std::chrono::milliseconds kill_timeout_;
  std::chrono::milliseconds multikill_timeout_;
  double multikill_threshold_;
  Protobuf::RepeatedPtrField<envoy::config::bootstrap::v3::Watchdog::WatchdogAction> actions_;
};

/**
 * Initial configuration that reads from JSON.
 */
class InitialImpl : public Initial {
public:
  InitialImpl(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
              absl::Status& creation_status);

  // Server::Configuration::Initial
  Admin& admin() override { return admin_; }
  absl::optional<std::string> flagsPath() const override { return flags_path_; }
  const envoy::config::bootstrap::v3::LayeredRuntime& runtime() override {
    return layered_runtime_;
  }

  /**
   * Initialize admin access log.
   */
  void initAdminAccessLog(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                          FactoryContext& factory_context);

private:
  struct AdminImpl : public Admin {
    // Server::Configuration::Initial::Admin
    const std::string& profilePath() const override { return profile_path_; }
    Network::Address::InstanceConstSharedPtr address() override { return address_; }
    Network::Socket::OptionsSharedPtr socketOptions() override { return socket_options_; }
    std::list<AccessLog::InstanceSharedPtr> accessLogs() const override { return access_logs_; }
    bool ignoreGlobalConnLimit() const override { return ignore_global_conn_limit_; }

    std::string profile_path_;
    std::list<AccessLog::InstanceSharedPtr> access_logs_;
    Network::Address::InstanceConstSharedPtr address_;
    Network::Socket::OptionsSharedPtr socket_options_;
    bool ignore_global_conn_limit_;
  };

  AdminImpl admin_;
  absl::optional<std::string> flags_path_;
  envoy::config::bootstrap::v3::LayeredRuntime layered_runtime_;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
