#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/network/address.h"
#include "envoy/stats/stats_options.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {

/**
 * Whether to run Envoy in serving mode, or in config validation mode at one of two levels (in which
 * case we'll verify the configuration file is valid, print any errors, and exit without serving.)
 */
enum class Mode {
  /**
   * Default mode: Regular Envoy serving process. Configs are validated in the normal course of
   * initialization, but if all is well we proceed to serve traffic.
   */
  Serve,

  /**
   * Validate as much as possible without opening network connections upstream or downstream.
   */
  Validate,

  /**
   * Completely load and initialize the config, and then exit without running the listener loop.
   */
  InitOnly,

  // TODO(rlazarus): Add a fourth option for "light validation": Mock out access to the filesystem.
  // Perform no validation of files referenced in the config, such as runtime configs, SSL certs,
  // etc. Validation will pass even if those files are malformed or don't exist, allowing the config
  // to be validated in a non-prod environment.
};

/**
 * General options for the server.
 */
class Options {
public:
  virtual ~Options() {}

  /**
   * @return uint64_t the base ID for the server. This is required for system-wide things like
   *         shared memory, domain sockets, etc. that are used during hot restart. Setting the
   *         base ID to a different value will allow the server to run multiple times on the same
   *         host if desired.
   */
  virtual uint64_t baseId() const PURE;

  /**
   * @return the number of worker threads to run in the server.
   */
  virtual uint32_t concurrency() const PURE;

  /**
   * @return the number of seconds that envoy will perform draining during a hot restart.
   */
  virtual std::chrono::seconds drainTime() const PURE;

  /**
   * @return const std::string& the path to the configuration file.
   */
  virtual const std::string& configPath() const PURE;

  /**
   * @return const std::string& an inline YAML bootstrap config that merges
   *                            into the config loaded in configPath().
   */
  virtual const std::string& configYaml() const PURE;

  /**
   * @return bool whether the config should only be parsed as v2. If false, when a v2 parse fails,
   *              a second attempt to parse the config as v1 will be made.
   */
  virtual bool v2ConfigOnly() const PURE;

  /**
   * @return const std::string& the admin address output file.
   */
  virtual const std::string& adminAddressPath() const PURE;

  /**
   * @return Network::Address::IpVersion the local address IP version.
   */
  virtual Network::Address::IpVersion localAddressIpVersion() const PURE;

  /**
   * @return spdlog::level::level_enum the default log level for the server.
   */
  virtual spdlog::level::level_enum logLevel() const PURE;

  /**
   * @return const std::vector<std::pair<std::string, spdlog::level::level_enum>>& pair of
   * component,log level for all configured components.
   */
  virtual const std::vector<std::pair<std::string, spdlog::level::level_enum>>&
  componentLogLevels() const PURE;

  /**
   * @return const std::string& the log format string.
   */
  virtual const std::string& logFormat() const PURE;

  /**
   * @return const std::string& the log file path.
   */
  virtual const std::string& logPath() const PURE;

  /**
   * @return the number of seconds that envoy will wait before shutting down the parent envoy during
   *         a host restart. Generally this will be longer than the drainTime() option.
   */
  virtual std::chrono::seconds parentShutdownTime() const PURE;

  /**
   * @return the restart epoch. 0 indicates the first server start, 1 the second, and so on.
   */
  virtual uint64_t restartEpoch() const PURE;

  /**
   * @return whether to verify the configuration file is valid, print any errors, and exit
   *         without serving.
   */
  virtual Mode mode() const PURE;

  /**
   * @return std::chrono::milliseconds the duration in msec between log flushes.
   */
  virtual std::chrono::milliseconds fileFlushIntervalMsec() const PURE;

  /**
   * @return const std::string& the server's cluster.
   */
  virtual const std::string& serviceClusterName() const PURE;

  /**
   * @return const std::string& the server's node identification.
   */
  virtual const std::string& serviceNodeName() const PURE;

  /**
   * @return const std::string& the server's zone.
   */
  virtual const std::string& serviceZone() const PURE;

  /**
   * @return uint64_t the maximum number of stats gauges and counters.
   */
  virtual uint64_t maxStats() const PURE;

  /**
   * @return StatsOptions& the max stat name / suffix lengths for stats.
   * router/cluster/listener.
   */
  virtual const Stats::StatsOptions& statsOptions() const PURE;

  /**
   * @return bool indicating whether the hot restart functionality has been disabled via cli flags.
   */
  virtual bool hotRestartDisabled() const PURE;

  /**
   * @return bool indicating whether system signal listeners are enabled.
   */
  virtual bool signalHandlingEnabled() const PURE;

  /**
   * @return bool indicating whether mutex tracing functionality has been enabled.
   */
  virtual bool mutexTracingEnabled() const PURE;
};

} // namespace Server
} // namespace Envoy
