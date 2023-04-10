#include "source/server/options_impl.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

#include "envoy/admin/v3/server_info.pb.h"

#include "source/common/common/fmt.h"
#include "source/common/common/logger.h"
#include "source/common/common/macros.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/tag_utility.h"
#include "source/common/version/version.h"
#include "source/server/options_impl_platform.h"

#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "spdlog/spdlog.h"
#include "tclap/CmdLine.h"

namespace Envoy {
namespace {
std::vector<std::string> toArgsVector(int argc, const char* const* argv) {
  std::vector<std::string> args;
  args.reserve(argc);

  for (int i = 0; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }
  return args;
}
} // namespace

OptionsImpl::OptionsImpl(int argc, const char* const* argv,
                         const HotRestartVersionCb& hot_restart_version_cb,
                         spdlog::level::level_enum default_log_level)
    : OptionsImpl(toArgsVector(argc, argv), hot_restart_version_cb, default_log_level) {}

OptionsImpl::OptionsImpl(std::vector<std::string> args,
                         const HotRestartVersionCb& hot_restart_version_cb,
                         spdlog::level::level_enum default_log_level) {
  std::string log_levels_string = fmt::format("Log levels: {}", allowedLogLevels());
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
  TCLAP::SwitchArg use_dynamic_base_id(
      "", "use-dynamic-base-id",
      "the server chooses a base ID dynamically. Supersedes a static base ID. May not be used "
      "when the restart epoch is non-zero.",
      cmd, false);
  TCLAP::ValueArg<std::string> base_id_path(
      "", "base-id-path", "path to which the base ID is written", false, "", "string", cmd);
  TCLAP::ValueArg<uint32_t> concurrency("", "concurrency", "# of worker threads to run", false,
                                        std::thread::hardware_concurrency(), "uint32_t", cmd);
  TCLAP::ValueArg<std::string> config_path("c", "config-path", "Path to configuration file", false,
                                           "", "string", cmd);
  TCLAP::ValueArg<std::string> config_yaml(
      "", "config-yaml", "Inline YAML configuration, merges with the contents of --config-path",
      false, "", "string", cmd);

  TCLAP::SwitchArg allow_unknown_fields("", "allow-unknown-fields",
                                        "allow unknown fields in static configuration (DEPRECATED)",
                                        cmd, false);
  TCLAP::SwitchArg allow_unknown_static_fields("", "allow-unknown-static-fields",
                                               "allow unknown fields in static configuration", cmd,
                                               false);
  TCLAP::SwitchArg reject_unknown_dynamic_fields("", "reject-unknown-dynamic-fields",
                                                 "reject unknown fields in dynamic configuration",
                                                 cmd, false);
  TCLAP::SwitchArg ignore_unknown_dynamic_fields("", "ignore-unknown-dynamic-fields",
                                                 "ignore unknown fields in dynamic configuration",
                                                 cmd, false);

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
  TCLAP::SwitchArg log_format_escaped("", "log-format-escaped",
                                      "Escape c-style escape sequences in the application logs",
                                      cmd, false);
  TCLAP::SwitchArg enable_fine_grain_logging(
      "", "enable-fine-grain-logging",
      "Logger mode: enable file level log control (Fine-Grain Logger) or not", cmd, false);
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
  TCLAP::ValueArg<uint32_t> drain_time_s("", "drain-time-s",
                                         "Hot restart and LDS removal drain time in seconds", false,
                                         600, "uint32_t", cmd);
  TCLAP::ValueArg<std::string> drain_strategy(
      "", "drain-strategy",
      "Hot restart drain sequence behaviour, one of 'gradual' (default) or 'immediate'.", false,
      "gradual", "string", cmd);
  TCLAP::ValueArg<uint32_t> parent_shutdown_time_s("", "parent-shutdown-time-s",
                                                   "Hot restart parent shutdown time in seconds",
                                                   false, 900, "uint32_t", cmd);
  TCLAP::ValueArg<std::string> mode("", "mode",
                                    "One of 'serve' (default; validate configs and then serve "
                                    "traffic normally) or 'validate' (validate configs and exit).",
                                    false, "serve", "string", cmd);
  TCLAP::SwitchArg disable_hot_restart("", "disable-hot-restart",
                                       "Disable hot restart functionality", cmd, false);
  TCLAP::SwitchArg enable_mutex_tracing(
      "", "enable-mutex-tracing", "Enable mutex contention tracing functionality", cmd, false);
  TCLAP::SwitchArg cpuset_threads(
      "", "cpuset-threads", "Get the default # of worker threads from cpuset size", cmd, false);

  TCLAP::ValueArg<std::string> disable_extensions("", "disable-extensions",
                                                  "Comma-separated list of extensions to disable",
                                                  false, "", "string", cmd);

  TCLAP::ValueArg<std::string> socket_path("", "socket-path", "Path to hot restart socket file",
                                           false, "@envoy_domain_socket", "string", cmd);

  TCLAP::ValueArg<std::string> socket_mode("", "socket-mode", "Socket file permission", false,
                                           "600", "string", cmd);
  TCLAP::SwitchArg enable_core_dump("", "enable-core-dump", "Enable core dumps", cmd, false);

  TCLAP::MultiArg<std::string> stats_tag(
      "", "stats-tag",
      "This flag provides a universal tag for all stats generated by Envoy. The format is "
      "``tag:value``. Only alphanumeric values are allowed for tag names. For tag values all "
      "characters are permitted except for '.' (dot). This flag can be repeated multiple times to "
      "set multiple universal tags. Multiple values for the same tag name are not allowed.",
      false, "string", cmd);

  cmd.setExceptionHandling(false);
  TRY_ASSERT_MAIN_THREAD {
    cmd.parse(args);
    count_ = cmd.getArgList().size();
  }
  END_TRY
  catch (TCLAP::ArgException& e) {
    TRY_ASSERT_MAIN_THREAD { cmd.getOutput()->failure(cmd, e); }
    END_TRY
    catch (const TCLAP::ExitException&) {
      // failure() has already written an informative message to stderr, so all that's left to do
      // is throw our own exception with the original message.
      throw MalformedArgvException(e.what());
    }
  }
  catch (const TCLAP::ExitException& e) {
    // parse() throws an ExitException with status 0 after printing the output for --help and
    // --version.
    throw NoServingException();
  }

  hot_restart_disabled_ = disable_hot_restart.getValue();
  mutex_tracing_enabled_ = enable_mutex_tracing.getValue();
  core_dump_enabled_ = enable_core_dump.getValue();

  cpuset_threads_ = cpuset_threads.getValue();

  if (log_level.isSet()) {
    log_level_ = parseAndValidateLogLevel(log_level.getValue());
  } else {
    log_level_ = default_log_level;
  }

  log_format_ = log_format.getValue();
  log_format_escaped_ = log_format_escaped.getValue();
  enable_fine_grain_logging_ = enable_fine_grain_logging.getValue();

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
  base_id_ = base_id.getValue();
  use_dynamic_base_id_ = use_dynamic_base_id.getValue();
  base_id_path_ = base_id_path.getValue();
  restart_epoch_ = restart_epoch.getValue();

  if (use_dynamic_base_id_ && restart_epoch_ > 0) {
    const std::string message = fmt::format(
        "error: cannot use --restart-epoch={} with --use-dynamic-base-id", restart_epoch_);
    throw MalformedArgvException(message);
  }

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
  if (allow_unknown_fields.getValue()) {
    ENVOY_LOG(warn,
              "--allow-unknown-fields is deprecated, use --allow-unknown-static-fields instead.");
  }
  allow_unknown_static_fields_ =
      allow_unknown_static_fields.getValue() || allow_unknown_fields.getValue();
  reject_unknown_dynamic_fields_ = reject_unknown_dynamic_fields.getValue();
  ignore_unknown_dynamic_fields_ = ignore_unknown_dynamic_fields.getValue();
  admin_address_path_ = admin_address_path.getValue();
  log_path_ = log_path.getValue();
  service_cluster_ = service_cluster.getValue();
  service_node_ = service_node.getValue();
  service_zone_ = service_zone.getValue();
  file_flush_interval_msec_ = std::chrono::milliseconds(file_flush_interval_msec.getValue());
  drain_time_ = std::chrono::seconds(drain_time_s.getValue());
  parent_shutdown_time_ = std::chrono::seconds(parent_shutdown_time_s.getValue());
  socket_path_ = socket_path.getValue();

  if (socket_path_.at(0) == '@') {
    socket_mode_ = 0;
  } else {
    uint64_t socket_mode_helper;
    if (!StringUtil::atoull(socket_mode.getValue().c_str(), socket_mode_helper, 8)) {
      throw MalformedArgvException(
          fmt::format("error: invalid socket-mode '{}'", socket_mode.getValue()));
    }
    socket_mode_ = socket_mode_helper;
  }

  if (drain_strategy.getValue() == "immediate") {
    drain_strategy_ = Server::DrainStrategy::Immediate;
  } else if (drain_strategy.getValue() == "gradual") {
    drain_strategy_ = Server::DrainStrategy::Gradual;
  } else {
    throw MalformedArgvException(
        fmt::format("error: unknown drain-strategy '{}'", mode.getValue()));
  }

  if (hot_restart_version_option.getValue()) {
    std::cerr << hot_restart_version_cb(!hot_restart_disabled_);
    throw NoServingException();
  }

  if (!disable_extensions.getValue().empty()) {
    disabled_extensions_ = absl::StrSplit(disable_extensions.getValue(), ',');
  }

  if (!stats_tag.getValue().empty()) {
    for (const auto& cli_tag_pair : stats_tag.getValue()) {

      std::vector<absl::string_view> cli_tag_pair_tokens =
          absl::StrSplit(cli_tag_pair, absl::MaxSplits(':', 1));
      if (cli_tag_pair_tokens.size() != 2) {
        throw MalformedArgvException(
            fmt::format("error: misformatted stats-tag '{}'", cli_tag_pair));
      }

      auto name = cli_tag_pair_tokens[0];
      if (!Stats::TagUtility::isTagNameValid(name)) {
        throw MalformedArgvException(
            fmt::format("error: misformatted stats-tag '{}' contains invalid char in '{}'",
                        cli_tag_pair, name));
      }

      auto value = cli_tag_pair_tokens[1];
      if (!Stats::TagUtility::isTagValueValid(value)) {
        throw MalformedArgvException(
            fmt::format("error: misformatted stats-tag '{}' contains invalid char in '{}'",
                        cli_tag_pair, value));
      }

      stats_tags_.emplace_back(Stats::Tag{std::string(name), std::string(value)});
    }
  }
}

spdlog::level::level_enum OptionsImpl::parseAndValidateLogLevel(absl::string_view log_level) {
  if (log_level == "warn") {
    return spdlog::level::level_enum::warn;
  }

  size_t level_to_use = std::numeric_limits<size_t>::max();
  for (size_t i = 0; i < ARRAY_SIZE(spdlog::level::level_string_views); i++) {
    spdlog::string_view_t spd_log_level = spdlog::level::level_string_views[i];
    if (log_level == absl::string_view(spd_log_level.data(), spd_log_level.size())) {
      level_to_use = i;
      break;
    }
  }

  if (level_to_use == std::numeric_limits<size_t>::max()) {
    logError(fmt::format("error: invalid log level specified '{}'", log_level));
  }
  return static_cast<spdlog::level::level_enum>(level_to_use);
}

std::string OptionsImpl::allowedLogLevels() {
  std::string allowed_log_levels;
  for (auto level_string_view : spdlog::level::level_string_views) {
    if (level_string_view == spdlog::level::to_string_view(spdlog::level::warn)) {
      allowed_log_levels += fmt::format("[{}|warn]", level_string_view);
    } else {
      allowed_log_levels += fmt::format("[{}]", level_string_view);
    }
  }
  return allowed_log_levels;
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
    spdlog::level::level_enum log_level = parseAndValidateLogLevel(log_name_level[1]);
    Logger::Logger* logger_to_change = Logger::Registry::logger(log_name);
    if (!logger_to_change) {
      logError(fmt::format("error: invalid component specified '{}'", log_name));
    }
    component_log_levels_.push_back(std::make_pair(log_name, log_level));
  }
}

uint32_t OptionsImpl::count() const { return count_; }

void OptionsImpl::logError(const std::string& error) { throw MalformedArgvException(error); }

Server::CommandLineOptionsPtr OptionsImpl::toCommandLineOptions() const {
  Server::CommandLineOptionsPtr command_line_options =
      std::make_unique<envoy::admin::v3::CommandLineOptions>();
  command_line_options->set_base_id(baseId());
  command_line_options->set_use_dynamic_base_id(useDynamicBaseId());
  command_line_options->set_base_id_path(baseIdPath());
  command_line_options->set_concurrency(concurrency());
  command_line_options->set_config_path(configPath());
  command_line_options->set_config_yaml(configYaml());
  command_line_options->set_allow_unknown_static_fields(allow_unknown_static_fields_);
  command_line_options->set_reject_unknown_dynamic_fields(reject_unknown_dynamic_fields_);
  command_line_options->set_ignore_unknown_dynamic_fields(ignore_unknown_dynamic_fields_);
  command_line_options->set_admin_address_path(adminAddressPath());
  command_line_options->set_component_log_level(component_log_level_str_);
  command_line_options->set_log_level(spdlog::level::to_string_view(logLevel()).data(),
                                      spdlog::level::to_string_view(logLevel()).size());
  command_line_options->set_log_format(logFormat());
  command_line_options->set_log_format_escaped(logFormatEscaped());
  command_line_options->set_enable_fine_grain_logging(enableFineGrainLogging());
  command_line_options->set_log_path(logPath());
  command_line_options->set_service_cluster(serviceClusterName());
  command_line_options->set_service_node(serviceNodeName());
  command_line_options->set_service_zone(serviceZone());
  if (mode() == Server::Mode::Serve) {
    command_line_options->set_mode(envoy::admin::v3::CommandLineOptions::Serve);
  } else if (mode() == Server::Mode::Validate) {
    command_line_options->set_mode(envoy::admin::v3::CommandLineOptions::Validate);
  } else {
    command_line_options->set_mode(envoy::admin::v3::CommandLineOptions::InitOnly);
  }
  if (localAddressIpVersion() == Network::Address::IpVersion::v4) {
    command_line_options->set_local_address_ip_version(envoy::admin::v3::CommandLineOptions::v4);
  } else {
    command_line_options->set_local_address_ip_version(envoy::admin::v3::CommandLineOptions::v6);
  }
  command_line_options->mutable_file_flush_interval()->MergeFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(fileFlushIntervalMsec().count()));

  command_line_options->mutable_drain_time()->MergeFrom(
      Protobuf::util::TimeUtil::SecondsToDuration(drainTime().count()));
  command_line_options->set_drain_strategy(drainStrategy() == Server::DrainStrategy::Immediate
                                               ? envoy::admin::v3::CommandLineOptions::Immediate
                                               : envoy::admin::v3::CommandLineOptions::Gradual);
  command_line_options->mutable_parent_shutdown_time()->MergeFrom(
      Protobuf::util::TimeUtil::SecondsToDuration(parentShutdownTime().count()));

  command_line_options->set_disable_hot_restart(hotRestartDisabled());
  command_line_options->set_enable_mutex_tracing(mutexTracingEnabled());
  command_line_options->set_cpuset_threads(cpusetThreadsEnabled());
  command_line_options->set_restart_epoch(restartEpoch());
  for (const auto& e : disabledExtensions()) {
    command_line_options->add_disabled_extensions(e);
  }
  command_line_options->set_socket_path(socketPath());
  command_line_options->set_socket_mode(socketMode());
  for (const auto& tag : statsTags()) {
    command_line_options->add_stats_tag(fmt::format("{}:{}", tag.name_, tag.value_));
  }
  return command_line_options;
}

OptionsImpl::OptionsImpl(const std::string& service_cluster, const std::string& service_node,
                         const std::string& service_zone, spdlog::level::level_enum log_level)
    : log_level_(log_level), service_cluster_(service_cluster), service_node_(service_node),
      service_zone_(service_zone) {}

void OptionsImpl::disableExtensions(const std::vector<std::string>& names) {
  for (const auto& name : names) {
    const std::vector<absl::string_view> parts = absl::StrSplit(name, absl::MaxSplits('/', 1));

    if (parts.size() != 2) {
      ENVOY_LOG_MISC(warn, "failed to disable invalid extension name '{}'", name);
      continue;
    }

    if (Registry::FactoryCategoryRegistry::disableFactory(parts[0], parts[1])) {
      ENVOY_LOG_MISC(info, "disabled extension '{}'", name);
    } else {
      ENVOY_LOG_MISC(warn, "failed to disable unknown extension '{}'", name);
    }
  }
}

} // namespace Envoy
