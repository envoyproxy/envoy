#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/admin/v3/server_info.pb.h"
#include "envoy/common/pure.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/network/address.h"

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

using CommandLineOptionsPtr = std::unique_ptr<envoy::admin::v3::CommandLineOptions>;

/**
 * General options for the server.
 */
class Options {
public:
  virtual ~Options() = default;

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
   * @return const envoy::config::bootstrap::v2::Bootstrap& a bootstrap proto object
   * that merges into the config last, after configYaml and configPath.
   */
  virtual const envoy::config::bootstrap::v3::Bootstrap& configProto() const PURE;

  /**
   * @return bool allow unknown fields in the static configuration?
   */
  virtual bool allowUnknownStaticFields() const PURE;

  /**
   * @return bool allow unknown fields in the dynamic configuration?
   */
  virtual bool rejectUnknownDynamicFields() const PURE;

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
   * @return const bool indicating whether to escape c-style escape sequences in logs.
   */
  virtual bool logFormatEscaped() const PURE;

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

  /**
   * @return whether to use the fake symbol table implementation.
   */
  virtual bool fakeSymbolTableEnabled() const PURE;

  /**
   * @return bool indicating whether cpuset size should determine the number of worker threads.
   */
  virtual bool cpusetThreadsEnabled() const PURE;

  /**
   * @return the names of extensions to disable.
   */
  virtual const std::vector<std::string>& disabledExtensions() const PURE;

  /**
   * Converts the Options in to CommandLineOptions proto message defined in server_info.proto.
   * @return CommandLineOptionsPtr the protobuf representation of the options.
   */
  virtual CommandLineOptionsPtr toCommandLineOptions() const PURE;
};

} // namespace Server
} // namespace Envoy
