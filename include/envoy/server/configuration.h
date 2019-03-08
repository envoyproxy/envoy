#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/stats/sink.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Configuration for local disk runtime support.
 */
class Runtime {
public:
  virtual ~Runtime() {}

  /**
   * @return const std::string& the root symlink to watch for swapping.
   */
  virtual const std::string& symlinkRoot() PURE;

  /**
   * @return const std::string& the subdirectory to load with runtime data.
   */
  virtual const std::string& subdirectory() PURE;

  /**
   * @return const std::string& the override subdirectory.
   * Read runtime values from subdirectory and overrideSubdirectory, overrideSubdirectory wins.
   */
  virtual const std::string& overrideSubdirectory() PURE;
};

/**
 * The main server configuration.
 */
class Main {
public:
  virtual ~Main() {}

  /**
   * @return Upstream::ClusterManager* singleton for use by the entire server.
   *         This will be nullptr if the cluster manager has not initialized yet.
   */
  virtual Upstream::ClusterManager* clusterManager() PURE;

  /**
   * @return Tracing::HttpTracer& singleton for use by the entire server.
   */
  virtual Tracing::HttpTracer& httpTracer() PURE;

  /**
   * @return std::list<Stats::SinkPtr>& the list of stats sinks initialized from the configuration.
   */
  virtual std::list<Stats::SinkPtr>& statsSinks() PURE;

  /**
   * @return std::chrono::milliseconds the time interval between flushing to configured stat sinks.
   *         The server latches counters.
   */
  virtual std::chrono::milliseconds statsFlushInterval() const PURE;

  /**
   * @return std::chrono::milliseconds the time interval after which we count a nonresponsive thread
   *         event as a "miss" statistic.
   */
  virtual std::chrono::milliseconds wdMissTimeout() const PURE;

  /**
   * @return std::chrono::milliseconds the time interval after which we count a nonresponsive thread
   *         event as a "mega miss" statistic.
   */
  virtual std::chrono::milliseconds wdMegaMissTimeout() const PURE;

  /**
   * @return std::chrono::milliseconds the time interval after which we kill the process due to a
   *         single nonresponsive thread.
   */
  virtual std::chrono::milliseconds wdKillTimeout() const PURE;

  /**
   * @return std::chrono::milliseconds the time interval after which we kill the process due to
   *         multiple nonresponsive threads.
   */
  virtual std::chrono::milliseconds wdMultiKillTimeout() const PURE;
};

/**
 * Admin configuration.
 */
class Admin {
public:
  virtual ~Admin() {}

  /**
   * @return const std::string& the admin access log path.
   */
  virtual const std::string& accessLogPath() PURE;

  /**
   * @return const std::string& profiler output path.
   */
  virtual const std::string& profilePath() PURE;

  /**
   * @return Network::Address::InstanceConstSharedPtr the server address.
   */
  virtual Network::Address::InstanceConstSharedPtr address() PURE;
};

/**
 * Initial configuration values that are needed before the main configuration load.
 */
class Initial {
public:
  virtual ~Initial() {}

  /**
   * @return Admin& the admin config.
   */
  virtual Admin& admin() PURE;

  /**
   * @return absl::optional<std::string> the path to look for flag files.
   */
  virtual absl::optional<std::string> flagsPath() PURE;

  /**
   * @return Runtime* the local disk runtime configuration or nullptr if there is no configuration.
   */
  virtual Runtime* runtime() PURE;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
