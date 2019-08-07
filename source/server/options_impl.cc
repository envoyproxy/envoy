#include "server/options_impl.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

#include "common/common/fmt.h"
#include "common/common/logger.h"
#include "common/common/macros.h"
#include "common/common/version.h"
#include "common/protobuf/utility.h"

#include "server/options_impl_platform.h"

#include "absl/strings/str_split.h"
#include "spdlog/spdlog.h"
#include "tclap/CmdLine.h"

namespace Envoy {
OptionsImpl::OptionsImpl(int argc, const char* const* argv,
                         const HotRestartVersionCb& hot_restart_version_cb,
                         spdlog::level::level_enum default_log_level)
    : signal_handling_enabled_(true) {
  std::string log_levels_string = "Log levels: ";
  for (auto level_string_view : spdlog::level::level_string_views) {
    log_levels_string += fmt::format("[{}]", level_string_view);
  }
  log_levels_string +=
      fmt::format("\nDefault is [{}]", spdlog::level::level_string_views[default_log_level]);

  const std::string component_log_level_string =
      "Comma separated list of component log levels. For example upstream:debug,config:trace";
  const std::string log_format_string =
      fmt::format("Log message format in spdlog syntax "
                  "(see https://github.com/gabime/spdlog/wiki/3.-Custom-formatting)"
                  "\nDefault is \"{}\"",
                  Logger::Logger::DEFAULT_LOG_FORMAT);

  TCLAP::CmdLine cmd("envoy", ' ', VersionInfo::version());
  TCLAP::ValueArg<uint32_t> base_id(
      "", "base-id", "base ID so that multiple envoys can run on the same host if needed", false, 0,
      "uint32_t", cmd);
  TCLAP::ValueArg<uint32_t> concurrency("", "concurrency", "# of worker threads to run", false,
                                        std::thread::hardware_concurrency(), "uint32_t", cmd);
  TCLAP::ValueArg<std::string> config_path("c", "config-path", "Path to configuration file", false,
                                           "", "string", cmd);
  TCLAP::ValueArg<std::string> config_yaml(
      "", "config-yaml", "Inline YAML configuration, merges with the contents of --config-path",
      false, "", "string", cmd);

  TCLAP::SwitchArg allow_unknown_fields("", "allow-unknown-fields",
                                        "allow unknown fields in the configuration", cmd, false);

  TCLAP::ValueArg<std::string> admin_address_path("", "admin-address-path", "Admin address path",
                                                  false, "", "string", cmd);
  TCLAP::ValueArg<std::string> local_address_ip_version("", "local-address-ip-version",
                                                        "The local "
                                                        "IP address version (v4 or v6).",
                                                        false, "v4", "string", cmd);
  TCLAP::ValueArg<std::string> log_level(
      "l", "log-level", log_levels_string, false,
      spdlog::level::level_string_views[default_log_level].data(), "string", cmd);
  TCLAP::ValueArg<std::string> component_log_level(
      "", "component-log-level", component_log_level_string, false, "", "string", cmd);
  TCLAP::ValueArg<std::string> log_format("", "log-format", log_format_string, false,
                                          Logger::Logger::DEFAULT_LOG_FORMAT, "string", cmd);
  TCLAP::ValueArg<std::string> log_path("", "log-path", "Path to logfile", false, "", "string",
                                        cmd);
  TCLAP::ValueArg<uint32_t> restart_epoch("", "restart-epoch", "hot restart epoch #", false, 0,
                                          "uint32_t", cmd);
  TCLAP::SwitchArg hot_restart_version_option("", "hot-restart-version",
                                              "hot restart compatibility version", cmd);
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
                                      "Deprecated and unused; please do not specify.", false, 123,
                                      "uint64_t", cmd);
  TCLAP::ValueArg<uint64_t> max_obj_name_len("", "max-obj-name-len",
                                             "Deprecated and unused; please do not specify.", false,
                                             123, "uint64_t", cmd);
  TCLAP::SwitchArg disable_hot_restart("", "disable-hot-restart",
                                       "Disable hot restart functionality", cmd, false);
  TCLAP::SwitchArg enable_mutex_tracing(
      "", "enable-mutex-tracing", "Enable mutex contention tracing functionality", cmd, false);
  TCLAP::SwitchArg cpuset_threads(
      "", "cpuset-threads", "Get the default # of worker threads from cpuset size", cmd, false);

  TCLAP::ValueArg<bool> use_libevent_buffer("", "use-libevent-buffers",
                                            "Use the original libevent buffer implementation",
                                            false, true, "bool", cmd);

  cmd.setExceptionHandling(false);
  try {
    cmd.parse(argc, argv);
    count_ = cmd.getArgList().size();
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

  hot_restart_disabled_ = disable_hot_restart.getValue();

  mutex_tracing_enabled_ = enable_mutex_tracing.getValue();

  libevent_buffer_enabled_ = use_libevent_buffer.getValue();
  cpuset_threads_ = cpuset_threads.getValue();

  log_level_ = default_log_level;
  for (size_t i = 0; i < ARRAY_SIZE(spdlog::level::level_string_views); i++) {
    if (log_level.getValue() == spdlog::level::level_string_views[i]) {
      log_level_ = static_cast<spdlog::level::level_enum>(i);
    }
  }

  log_format_ = log_format.getValue();

  parseComponentLogLevels(component_log_level.getValue());

  if (mode.getValue() == "serve") {
    mode_ = Server::Mode::Serve;
  } else if (mode.getValue() == "validate") {
    mode_ = Server::Mode::Validate;
  } else if (mode.getValue() == "init_only") {
    mode_ = Server::Mode::InitOnly;
  } else {
    const std::string message = fmt::format("error: unknown mode '{}'", mode.getValue());
    throw MalformedArgvException(message);
  }

  if (local_address_ip_version.getValue() == "v4") {
    local_address_ip_version_ = Network::Address::IpVersion::v4;
  } else if (local_address_ip_version.getValue() == "v6") {
    local_address_ip_version_ = Network::Address::IpVersion::v6;
  } else {
    const std::string message =
        fmt::format("error: unknown IP address version '{}'", local_address_ip_version.getValue());
    throw MalformedArgvException(message);
  }

  // For base ID, scale what the user inputs by 10 so that we have spread for domain sockets.
  base_id_ = base_id.getValue() * 10;

  if (!concurrency.isSet() && cpuset_threads_) {
    // The 'concurrency' command line option wasn't set but the 'cpuset-threads'
    // option was set. Use the number of CPUs assigned to the process cpuset, if
    // that can be known.
    concurrency_ = OptionsImplPlatform::getCpuCount();
  } else {
    if (concurrency.isSet() && cpuset_threads_ && cpuset_threads.isSet()) {
      ENVOY_LOG(warn, "Both --concurrency and --cpuset-threads options are set; not applying "
                      "--cpuset-threads.");
    }
    concurrency_ = std::max(1U, concurrency.getValue());
  }

  config_path_ = config_path.getValue();
  config_yaml_ = config_yaml.getValue();
  allow_unknown_fields_ = allow_unknown_fields.getValue();
  admin_address_path_ = admin_address_path.getValue();
  log_path_ = log_path.getValue();
  restart_epoch_ = restart_epoch.getValue();
  service_cluster_ = service_cluster.getValue();
  service_node_ = service_node.getValue();
  service_zone_ = service_zone.getValue();
  file_flush_interval_msec_ = std::chrono::milliseconds(file_flush_interval_msec.getValue());
  drain_time_ = std::chrono::seconds(drain_time_s.getValue());
  parent_shutdown_time_ = std::chrono::seconds(parent_shutdown_time_s.getValue());

  if (hot_restart_version_option.getValue()) {
    std::cerr << hot_restart_version_cb(!hot_restart_disabled_);
    throw NoServingException();
  }
}

void OptionsImpl::parseComponentLogLevels(const std::string& component_log_levels) {
  if (component_log_levels.empty()) {
    return;
  }
  component_log_level_str_ = component_log_levels;
  std::vector<std::string> log_levels = absl::StrSplit(component_log_levels, ',');
  for (auto& level : log_levels) {
    std::vector<std::string> log_name_level = absl::StrSplit(level, ':');
    if (log_name_level.size() != 2) {
      logError(fmt::format("error: component log level not correctly specified '{}'", level));
    }
    std::string log_name = log_name_level[0];
    std::string log_level = log_name_level[1];
    size_t level_to_use = std::numeric_limits<size_t>::max();
    for (size_t i = 0; i < ARRAY_SIZE(spdlog::level::level_string_views); i++) {
      if (log_level == spdlog::level::level_string_views[i]) {
        level_to_use = i;
        break;
      }
    }
    if (level_to_use == std::numeric_limits<size_t>::max()) {
      logError(fmt::format("error: invalid log level specified '{}'", log_level));
    }
    Logger::Logger* logger_to_change = Logger::Registry::logger(log_name);
    if (!logger_to_change) {
      logError(fmt::format("error: invalid component specified '{}'", log_name));
    }
    component_log_levels_.push_back(
        std::make_pair(log_name, static_cast<spdlog::level::level_enum>(level_to_use)));
  }
}

uint32_t OptionsImpl::count() const { return count_; }

void OptionsImpl::logError(const std::string& error) const { throw MalformedArgvException(error); }

Server::CommandLineOptionsPtr OptionsImpl::toCommandLineOptions() const {
  Server::CommandLineOptionsPtr command_line_options =
      std::make_unique<envoy::admin::v2alpha::CommandLineOptions>();
  command_line_options->set_base_id(baseId());
  command_line_options->set_concurrency(concurrency());
  command_line_options->set_config_path(configPath());
  command_line_options->set_config_yaml(configYaml());
  command_line_options->set_allow_unknown_fields(allow_unknown_fields_);
  command_line_options->set_admin_address_path(adminAddressPath());
  command_line_options->set_component_log_level(component_log_level_str_);
  command_line_options->set_log_level(spdlog::level::to_string_view(logLevel()).data(),
                                      spdlog::level::to_string_view(logLevel()).size());
  command_line_options->set_log_format(logFormat());
  command_line_options->set_log_path(logPath());
  command_line_options->set_service_cluster(serviceClusterName());
  command_line_options->set_service_node(serviceNodeName());
  command_line_options->set_service_zone(serviceZone());
  if (mode() == Server::Mode::Serve) {
    command_line_options->set_mode(envoy::admin::v2alpha::CommandLineOptions::Serve);
  } else if (mode() == Server::Mode::Validate) {
    command_line_options->set_mode(envoy::admin::v2alpha::CommandLineOptions::Validate);
  } else {
    command_line_options->set_mode(envoy::admin::v2alpha::CommandLineOptions::InitOnly);
  }
  if (localAddressIpVersion() == Network::Address::IpVersion::v4) {
    command_line_options->set_local_address_ip_version(
        envoy::admin::v2alpha::CommandLineOptions::v4);
  } else {
    command_line_options->set_local_address_ip_version(
        envoy::admin::v2alpha::CommandLineOptions::v6);
  }
  command_line_options->mutable_file_flush_interval()->MergeFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(fileFlushIntervalMsec().count()));
  command_line_options->mutable_parent_shutdown_time()->MergeFrom(
      Protobuf::util::TimeUtil::SecondsToDuration(parentShutdownTime().count()));
  command_line_options->mutable_drain_time()->MergeFrom(
      Protobuf::util::TimeUtil::SecondsToDuration(drainTime().count()));
  command_line_options->set_disable_hot_restart(hotRestartDisabled());
  command_line_options->set_enable_mutex_tracing(mutexTracingEnabled());
  command_line_options->set_cpuset_threads(cpusetThreadsEnabled());
  command_line_options->set_restart_epoch(restartEpoch());
  return command_line_options;
}

OptionsImpl::OptionsImpl(const std::string& service_cluster, const std::string& service_node,
                         const std::string& service_zone, spdlog::level::level_enum log_level)
    : base_id_(0u), concurrency_(1u), config_path_(""), config_yaml_(""),
      local_address_ip_version_(Network::Address::IpVersion::v4), log_level_(log_level),
      log_format_(Logger::Logger::DEFAULT_LOG_FORMAT), restart_epoch_(0u),
      service_cluster_(service_cluster), service_node_(service_node), service_zone_(service_zone),
      file_flush_interval_msec_(10000), drain_time_(600), parent_shutdown_time_(900),
      mode_(Server::Mode::Serve), hot_restart_disabled_(false), signal_handling_enabled_(true),
      mutex_tracing_enabled_(false), cpuset_threads_(false), libevent_buffer_enabled_(false) {}

} // namespace Envoy
