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
#include "envoy/http/filter.h"
#include "envoy/network/filter.h"
#include "envoy/server/configuration.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/instance.h"

#include "common/common/logger.h"
#include "common/json/json_loader.h"
#include "common/network/resolver_impl.h"
#include "common/network/utility.h"

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
                               const std::vector<Network::FilterFactoryCb>& factories);

  /**
   * Given a ListenerFilterManager and a list of factories, create a new filter chain. Chain
   * creation will exit early if any filters immediately close the connection.
   *
   * TODO(sumukhs): Coalesce with the above as they are very similar
   */
  static bool buildFilterChain(Network::ListenerFilterManager& filter_manager,
                               const std::vector<Network::ListenerFilterFactoryCb>& factories);

  /**
   * Given a UdpListenerFilterManager and a list of factories, create a new filter chain. Chain
   * creation will exit early if any filters immediately close the connection.
   */
  static void
  buildUdpFilterChain(Network::UdpListenerFilterManager& filter_manager,
                      Network::UdpReadFilterCallbacks& callbacks,
                      const std::vector<Network::UdpListenerFilterFactoryCb>& factories);
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
   */
  void initialize(const envoy::config::bootstrap::v3::Bootstrap& bootstrap, Instance& server,
                  Upstream::ClusterManagerFactory& cluster_manager_factory);

  // Server::Configuration::Main
  Upstream::ClusterManager* clusterManager() override { return cluster_manager_.get(); }
  std::list<Stats::SinkPtr>& statsSinks() override { return stats_sinks_; }
  std::chrono::milliseconds statsFlushInterval() const override { return stats_flush_interval_; }
  std::chrono::milliseconds wdMissTimeout() const override { return watchdog_miss_timeout_; }
  std::chrono::milliseconds wdMegaMissTimeout() const override {
    return watchdog_megamiss_timeout_;
  }
  std::chrono::milliseconds wdKillTimeout() const override { return watchdog_kill_timeout_; }
  std::chrono::milliseconds wdMultiKillTimeout() const override {
    return watchdog_multikill_timeout_;
  }

  double wdMultiKillThreshold() const override { return watchdog_multikill_threshold_; }
  Protobuf::RepeatedPtrField<envoy::config::bootstrap::v3::Watchdog::WatchdogAction>
  wdActions() const override {
    return watchdog_actions_;
  }

private:
  /**
   * Initialize tracers and corresponding sinks.
   */
  void initializeTracers(const envoy::config::trace::v3::Tracing& configuration, Instance& server);

  void initializeStatsSinks(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                            Instance& server);

  std::unique_ptr<Upstream::ClusterManager> cluster_manager_;
  std::list<Stats::SinkPtr> stats_sinks_;
  std::chrono::milliseconds stats_flush_interval_;
  std::chrono::milliseconds watchdog_miss_timeout_;
  std::chrono::milliseconds watchdog_megamiss_timeout_;
  std::chrono::milliseconds watchdog_kill_timeout_;
  std::chrono::milliseconds watchdog_multikill_timeout_;
  double watchdog_multikill_threshold_;
  Protobuf::RepeatedPtrField<envoy::config::bootstrap::v3::Watchdog::WatchdogAction>
      watchdog_actions_;
};

/**
 * Initial configuration that reads from JSON.
 */
class InitialImpl : public Initial {
public:
  InitialImpl(const envoy::config::bootstrap::v3::Bootstrap& bootstrap);

  // Server::Configuration::Initial
  Admin& admin() override { return admin_; }
  absl::optional<std::string> flagsPath() const override { return flags_path_; }
  const envoy::config::bootstrap::v3::LayeredRuntime& runtime() override {
    return layered_runtime_;
  }

private:
  struct AdminImpl : public Admin {
    // Server::Configuration::Initial::Admin
    const std::string& accessLogPath() const override { return access_log_path_; }
    const std::string& profilePath() const override { return profile_path_; }
    Network::Address::InstanceConstSharedPtr address() override { return address_; }
    Network::Socket::OptionsSharedPtr socketOptions() override { return socket_options_; }

    std::string access_log_path_;
    std::string profile_path_;
    Network::Address::InstanceConstSharedPtr address_;
    Network::Socket::OptionsSharedPtr socket_options_;
  };

  AdminImpl admin_;
  absl::optional<std::string> flags_path_;
  envoy::config::bootstrap::v3::LayeredRuntime layered_runtime_;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
