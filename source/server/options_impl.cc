#include "server/options_impl.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

#include "common/common/fmt.h"
#include "common/common/macros.h"
#include "common/common/version.h"
#include "common/stats/stats_impl.h"

#include "spdlog/spdlog.h"
#include "tclap/CmdLine.h"

// Can be overridden at compile time
#ifndef ENVOY_DEFAULT_MAX_STATS
#define ENVOY_DEFAULT_MAX_STATS 16384
#endif

// Can be overridden at compile time
// See comment in common/stat/stat_impl.h for rationale behind
// this constant.
#ifndef ENVOY_DEFAULT_MAX_OBJ_NAME_LENGTH
#define ENVOY_DEFAULT_MAX_OBJ_NAME_LENGTH 60
#endif

#if ENVOY_DEFAULT_MAX_OBJ_NAME_LENGTH < 60
#error "ENVOY_DEFAULT_MAX_OBJ_NAME_LENGTH must be >= 60"
#endif

namespace Envoy {
OptionsImpl::OptionsImpl(int argc, char** argv, const HotRestartVersionCb& hot_restart_version_cb,
                         spdlog::level::level_enum default_log_level) {
  std::string log_levels_string = "Log levels: ";
  for (size_t i = 0; i < ARRAY_SIZE(spdlog::level::level_names); i++) {
    log_levels_string += fmt::format("[{}]", spdlog::level::level_names[i]);
  }
  log_levels_string +=
      fmt::format("\nDefault is [{}]", spdlog::level::level_names[default_log_level]);
  log_levels_string += "\n[trace] and [debug] are only available on debug builds";

  TCLAP::CmdLine cmd("envoy", ' ', VersionInfo::version());
  TCLAP::ValueArg<uint32_t> base_id(
      "", "base-id", "base ID so that multiple envoys can run on the same host if needed", false, 0,
      "uint32_t", cmd);
  TCLAP::ValueArg<uint32_t> concurrency("", "concurrency", "# of worker threads to run", false,
                                        std::thread::hardware_concurrency(), "uint32_t", cmd);
  TCLAP::ValueArg<std::string> config_path("c", "config-path", "Path to configuration file", false,
                                           "", "string", cmd);
  TCLAP::SwitchArg v2_config_only("", "v2-config-only", "parse config as v2 only", cmd, false);
  TCLAP::ValueArg<std::string> admin_address_path("", "admin-address-path", "Admin address path",
                                                  false, "", "string", cmd);
  TCLAP::ValueArg<std::string> local_address_ip_version("", "local-address-ip-version",
                                                        "The local "
                                                        "IP address version (v4 or v6).",
                                                        false, "v4", "string", cmd);
  TCLAP::ValueArg<std::string> log_level("l", "log-level", log_levels_string, false,
                                         spdlog::level::level_names[default_log_level], "string",
                                         cmd);
  TCLAP::ValueArg<std::string> log_path("", "log-path", "Path to logfile", false, "", "string",
                                        cmd);
  TCLAP::ValueArg<uint32_t> restart_epoch("", "restart-epoch", "hot restart epoch #", false, 0,
                                          "uint32_t", cmd);
  TCLAP::SwitchArg hot_restart_version_option("", "hot-restart-version",
                                              "hot restart compatability version", cmd);
  TCLAP::ValueArg<std::string> service_cluster("", "service-cluster", "Cluster name", false, "",
                                               "string", cmd);
  TCLAP::ValueArg<std::string> service_node("", "service-node", "Node name", false, "", "string",
                                            cmd);
  TCLAP::ValueArg<std::string> service_zone("", "service-zone", "Zone name", false, "", "string",
                                            cmd);
  TCLAP::ValueArg<uint32_t> file_flush_interval_msec("", "file-flush-interval-msec",
                                                     "Interval for log flushing in msec", false,
                                                     10000, "uint32_t", cmd);
  TCLAP::ValueArg<uint32_t> drain_time_s("", "drain-time-s", "Hot restart drain time in seconds",
                                         false, 600, "uint32_t", cmd);
  TCLAP::ValueArg<uint32_t> parent_shutdown_time_s("", "parent-shutdown-time-s",
                                                   "Hot restart parent shutdown time in seconds",
                                                   false, 900, "uint32_t", cmd);
  TCLAP::ValueArg<std::string> mode("", "mode",
                                    "One of 'serve' (default; validate configs and then serve "
                                    "traffic normally) or 'validate' (validate configs and exit).",
                                    false, "serve", "string", cmd);
  TCLAP::ValueArg<uint64_t> max_stats("", "max-stats",
                                      "Maximum number of stats guages and counters "
                                      "that can be allocated in shared memory.",
                                      false, ENVOY_DEFAULT_MAX_STATS, "uint64_t", cmd);
  TCLAP::ValueArg<uint64_t> max_obj_name_len("", "max-obj-name-len",
                                             "Maximum name length for a field in the config "
                                             "(applies to listener name, route config name and"
                                             " the cluster name)",
                                             false, ENVOY_DEFAULT_MAX_OBJ_NAME_LENGTH, "uint64_t",
                                             cmd);

  cmd.setExceptionHandling(false);
  try {
    cmd.parse(argc, argv);
  } catch (TCLAP::ArgException& e) {
    try {
      cmd.getOutput()->failure(cmd, e);
    } catch (const TCLAP::ExitException&) {
      // failure() has already written an informative message to stderr, so all that's left to do
      // is throw our own exception with the original message.
      throw MalformedArgvException(e.what());
    }
  } catch (const TCLAP::ExitException& e) {
    // parse() throws an ExitException with status 0 after printing the output for --help and
    // --version.
    throw NoServingException();
  }

  if (max_obj_name_len.getValue() < 60) {
    const std::string message = fmt::format(
        "error: the 'max-obj-name-len' value specified ({}) is less than the minimum value of 60",
        max_obj_name_len.getValue());
    std::cerr << message << std::endl;
    throw MalformedArgvException(message);
  }

  if (hot_restart_version_option.getValue()) {
    std::cerr << hot_restart_version_cb(max_stats.getValue(),
                                        max_obj_name_len.getValue() +
                                            Stats::RawStatData::maxStatSuffixLength());
    throw NoServingException();
  }

  log_level_ = default_log_level;
  for (size_t i = 0; i < ARRAY_SIZE(spdlog::level::level_names); i++) {
    if (log_level.getValue() == spdlog::level::level_names[i]) {
      log_level_ = static_cast<spdlog::level::level_enum>(i);
    }
  }

  if (mode.getValue() == "serve") {
    mode_ = Server::Mode::Serve;
  } else if (mode.getValue() == "validate") {
    mode_ = Server::Mode::Validate;
  } else {
    const std::string message = fmt::format("error: unknown mode '{}'", mode.getValue());
    std::cerr << message << std::endl;
    throw MalformedArgvException(message);
  }

  if (local_address_ip_version.getValue() == "v4") {
    local_address_ip_version_ = Network::Address::IpVersion::v4;
  } else if (local_address_ip_version.getValue() == "v6") {
    local_address_ip_version_ = Network::Address::IpVersion::v6;
  } else {
    const std::string message =
        fmt::format("error: unknown IP address version '{}'", local_address_ip_version.getValue());
    std::cerr << message << std::endl;
    throw MalformedArgvException(message);
  }

  // For base ID, scale what the user inputs by 10 so that we have spread for domain sockets.
  base_id_ = base_id.getValue() * 10;
  concurrency_ = concurrency.getValue();
  config_path_ = config_path.getValue();
  v2_config_only_ = v2_config_only.getValue();
  admin_address_path_ = admin_address_path.getValue();
  log_path_ = log_path.getValue();
  restart_epoch_ = restart_epoch.getValue();
  service_cluster_ = service_cluster.getValue();
  service_node_ = service_node.getValue();
  service_zone_ = service_zone.getValue();
  file_flush_interval_msec_ = std::chrono::milliseconds(file_flush_interval_msec.getValue());
  drain_time_ = std::chrono::seconds(drain_time_s.getValue());
  parent_shutdown_time_ = std::chrono::seconds(parent_shutdown_time_s.getValue());
  max_stats_ = max_stats.getValue();
  max_obj_name_length_ = max_obj_name_len.getValue();
}
} // namespace Envoy
