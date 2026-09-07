#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/options.h"

#include "source/common/common/base_logger.h"
#include "source/common/common/logger.h"
#include "source/common/config/well_known_names.h"
#include "source/server/options_impl_base.h"

#include "spdlog/spdlog.h"

namespace Envoy {

/**
 * Implementation of Server::Options which can parse from the command line.
 */
class OptionsImpl : public OptionsImplBase {
public:
  /**
   * Parameters are hot_restart_enabled
   */
  using HotRestartVersionCb = std::function<std::string(bool)>;

  /**
   * @throw NoServingException if Envoy has already done everything specified by the args (e.g.
   *        print the hot restart version) and it's time to exit without serving HTTP traffic. The
   *        caller should exit(0) after any necessary cleanup.
   * @throw MalformedArgvException if something is wrong with the arguments (invalid flag or flag
   *        value). The caller should call exit(1) after any necessary cleanup.
   */
  [[deprecated("Use constructor with Logger::Levels")]] OptionsImpl(
      int argc, const char* const* argv, const HotRestartVersionCb& hot_restart_version_cb,
      spdlog::level::level_enum default_log_level);
  OptionsImpl(int argc, const char* const* argv, const HotRestartVersionCb& hot_restart_version_cb,
              Logger::Levels default_log_level);

  /**
   * @throw NoServingException if Envoy has already done everything specified by the args (e.g.
   *        print the hot restart version) and it's time to exit without serving HTTP traffic. The
   *        caller should exit(0) after any necessary cleanup.
   * @throw MalformedArgvException if something is wrong with the arguments (invalid flag or flag
   *        value). The caller should call exit(1) after any necessary cleanup.
   */
  [[deprecated("Use constructor with Logger::Levels")]] OptionsImpl(
      std::vector<std::string> args, const HotRestartVersionCb& hot_restart_version_cb,
      spdlog::level::level_enum default_log_level);
  OptionsImpl(std::vector<std::string> args, const HotRestartVersionCb& hot_restart_version_cb,
              Logger::Levels default_log_level);

  // Default constructor; creates "reasonable" defaults, but desired values should be set
  // explicitly.
  [[deprecated("Use constructor with Logger::Levels")]] OptionsImpl(
      const std::string& service_cluster, const std::string& service_node,
      const std::string& service_zone, spdlog::level::level_enum log_level);
  OptionsImpl(const std::string& service_cluster, const std::string& service_node,
              const std::string& service_zone, Logger::Levels log_level);

  Server::CommandLineOptionsPtr toCommandLineOptions() const override;
  void parseComponentLogLevels(const std::string& component_log_levels);
  static void logError(const std::string& error);
  static std::string allowedLogLevels();
};

/**
 * Thrown when an OptionsImpl was not constructed because all of Envoy's work is done (for example,
 * it was started with --help and it's already printed a help message) so all that's left to do is
 * exit successfully.
 */
class NoServingException : public EnvoyException {
public:
  NoServingException(const std::string& what) : EnvoyException(what) {}
};

/**
 * Thrown when an OptionsImpl was not constructed because the argv was invalid.
 */
class MalformedArgvException : public EnvoyException {
public:
  MalformedArgvException(const std::string& what) : EnvoyException(what) {}
};

} // namespace Envoy
