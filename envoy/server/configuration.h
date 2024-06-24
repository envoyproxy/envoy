#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/stats/sink.h"
#include "envoy/upstream/cluster_manager.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/*
 * Watchdog configuration.
 */
class Watchdog {
public:
  virtual ~Watchdog() = default;

  /**
   * @return std::chrono::milliseconds the time interval after which we count a nonresponsive thread
   *         event as a "miss" statistic.
   */
  virtual std::chrono::milliseconds missTimeout() const PURE;

  /**
   * @return std::chrono::milliseconds the time interval after which we count a nonresponsive thread
   *         event as a "mega miss" statistic.
   */
  virtual std::chrono::milliseconds megaMissTimeout() const PURE;

  /**
   * @return std::chrono::milliseconds the time interval after which we kill the process due to a
   *         single nonresponsive thread.
   */
  virtual std::chrono::milliseconds killTimeout() const PURE;

  /**
   * @return std::chrono::milliseconds the time interval after which we kill the process due to
   *         multiple nonresponsive threads.
   */
  virtual std::chrono::milliseconds multiKillTimeout() const PURE;

  /**
   * @return double the percentage of threads that need to meet the MultiKillTimeout before we
   *         kill the process. This is used in the calculation below
   *         Max(2, ceil(registered_threads * Fraction(MultiKillThreshold)))
   *         which computes the number of threads that need to be be nonresponsive
   *         for at least MultiKillTimeout before we kill the process.
   */
  virtual double multiKillThreshold() const PURE;

  /**
   * @return Protobuf::RepeatedPtrField<envoy::config::bootstrap::v3::Watchdog::WatchdogAction>
   *         the WatchDog Actions that trigger on WatchDog Events.
   */
  virtual Protobuf::RepeatedPtrField<envoy::config::bootstrap::v3::Watchdog::WatchdogAction>
  actions() const PURE;
};

class StatsConfig {
public:
  virtual ~StatsConfig() = default;

  /**
   * @return std::list<Stats::SinkPtr>& the list of stats sinks initialized from the configuration.
   */
  virtual const std::list<Stats::SinkPtr>& sinks() const PURE;

  /**
   * @return std::chrono::milliseconds the time interval between flushing to configured stat sinks.
   *         The server latches counters.
   */
  virtual std::chrono::milliseconds flushInterval() const PURE;

  /**
   * @return bool indicator to flush stats on-demand via the admin interface instead of on a timer.
   */
  virtual bool flushOnAdmin() const PURE;

  /**
   * @return true if deferred creation of stats is enabled.
   */
  virtual bool enableDeferredCreationStats() const PURE;
};

/**
 * The main server configuration.
 */
class Main {
public:
  virtual ~Main() = default;

  /**
   * @return Upstream::ClusterManager* singleton for use by the entire server.
   *         This will be nullptr if the cluster manager has not initialized yet.
   */
  virtual Upstream::ClusterManager* clusterManager() PURE;

  /**
   * @return const Upstream::ClusterManager* singleton for use by the entire server.
   *         This will be nullptr if the cluster manager has not initialized yet.
   */
  virtual const Upstream::ClusterManager* clusterManager() const PURE;

  /**
   * @return const StatsConfig& the configuration of server stats.
   */
  virtual StatsConfig& statsConfig() PURE;

  /**
   * @return const Watchdog& the configuration of the main thread watchdog.
   */
  virtual const Watchdog& mainThreadWatchdogConfig() const PURE;

  /**
   * @return const Watchdog& the configuration of the worker watchdog.
   */
  virtual const Watchdog& workerWatchdogConfig() const PURE;
};

/**
 * Admin configuration.
 */
class Admin {
public:
  virtual ~Admin() = default;

  /**
   * @return std::list<AccessLog::InstanceSharedPtr> the list of access loggers.
   */
  virtual std::list<AccessLog::InstanceSharedPtr> accessLogs() const PURE;

  /**
   * @return const std::string& profiler output path.
   */
  virtual const std::string& profilePath() const PURE;

  /**
   * @return Network::Address::InstanceConstSharedPtr the server address.
   */
  virtual Network::Address::InstanceConstSharedPtr address() PURE;

  /**
   * @return Network::Address::OptionsSharedPtr the list of listener socket options.
   */
  virtual Network::Socket::OptionsSharedPtr socketOptions() PURE;

  /**
   * @return bool whether the listener should avoid blocking connections based on the globally set
   * limit.
   */
  virtual bool ignoreGlobalConnLimit() const PURE;
};

/**
 * Initial configuration values that are needed before the main configuration load.
 */
class Initial {
public:
  virtual ~Initial() = default;

  /**
   * @return Admin& the admin config.
   */
  virtual Admin& admin() PURE;

  /**
   * @return absl::optional<std::string> the path to look for flag files.
   */
  virtual absl::optional<std::string> flagsPath() const PURE;

  /**
   * @return const envoy::config::bootstrap::v2::LayeredRuntime& runtime
   *         configuration.
   */
  virtual const envoy::config::bootstrap::v3::LayeredRuntime& runtime() PURE;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
