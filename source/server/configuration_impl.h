#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
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
class StatsSinkFactory {
public:
  virtual ~StatsSinkFactory() {}

  /**
   * Create a particular Stats::Sink implementation. If the implementation is unable to produce a
   * Stats::Sink with the provided parameters, it should throw an EnvoyException. The returned
   * pointer should always be valid.
   * @param config supplies the custom proto configuration for the Stats::Sink
   * @param server supplies the server instance
   */
  virtual Stats::SinkPtr createStatsSink(const Protobuf::Message& config, Instance& server) PURE;

  /**
   * @return ProtobufTypes::MessagePtr create empty config proto message for v2. The filter
   *         config, which arrives in an opaque google.protobuf.Struct message, will be converted to
   *         JSON and then parsed into this empty proto.
   */
  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() PURE;

  /**
   * Returns the identifying name for a particular implementation of Stats::Sink produced by the
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
                               const std::vector<Network::FilterFactoryCb>& factories);

  /**
   * Given a ListenerFilterManager and a list of factories, create a new filter chain. Chain
   * creation will exit early if any filters immediately close the connection.
   */
  static bool buildFilterChain(Network::ListenerFilterManager& filter_manager,
                               const std::vector<Network::ListenerFilterFactoryCb>& factories);
};

/**
 * Implementation of Server::Configuration::Main that reads a configuration from a JSON file.
 */
class MainImpl : Logger::Loggable<Logger::Id::config>, public Main {
public:
  /**
   * Initialize the configuration. This happens here vs. the constructor because the initialization
   * will call through the server into the config to get the cluster manager so the config object
   * must be created already.
   * @param bootstrap v2 bootstrap proto.
   * @param server supplies the owning server.
   * @param cluster_manager_factory supplies the cluster manager creation factory.
   */
  void initialize(const envoy::config::bootstrap::v2::Bootstrap& bootstrap, Instance& server,
                  Upstream::ClusterManagerFactory& cluster_manager_factory);

  // Server::Configuration::Main
  Upstream::ClusterManager* clusterManager() override { return cluster_manager_.get(); }
  Tracing::HttpTracer& httpTracer() override { return *http_tracer_; }
  RateLimit::ClientFactory& rateLimitClientFactory() override { return *ratelimit_client_factory_; }
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

private:
  /**
   * Initialize tracers and corresponding sinks.
   */
  void initializeTracers(const envoy::config::trace::v2::Tracing& configuration, Instance& server);

  void initializeStatsSinks(const envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                            Instance& server);

  std::unique_ptr<Upstream::ClusterManager> cluster_manager_;
  Tracing::HttpTracerPtr http_tracer_;
  std::list<Stats::SinkPtr> stats_sinks_;
  RateLimit::ClientFactoryPtr ratelimit_client_factory_;
  std::chrono::milliseconds stats_flush_interval_;
  std::chrono::milliseconds watchdog_miss_timeout_;
  std::chrono::milliseconds watchdog_megamiss_timeout_;
  std::chrono::milliseconds watchdog_kill_timeout_;
  std::chrono::milliseconds watchdog_multikill_timeout_;
};

/**
 * Initial configuration that reads from JSON.
 */
class InitialImpl : public Initial {
public:
  InitialImpl(const envoy::config::bootstrap::v2::Bootstrap& bootstrap);

  // Server::Configuration::Initial
  Admin& admin() override { return admin_; }
  absl::optional<std::string> flagsPath() override { return flags_path_; }
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
  absl::optional<std::string> flags_path_;
  std::unique_ptr<RuntimeImpl> runtime_;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
