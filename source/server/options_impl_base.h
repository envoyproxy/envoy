#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/options.h"

#include "source/common/common/logger.h"
#include "source/common/config/well_known_names.h"

#include "spdlog/spdlog.h"

namespace Envoy {

/**
 * Implementation of Server::Options without command line parsing.
 */
class OptionsImplBase : public Server::Options, protected Logger::Loggable<Logger::Id::config> {
public:
  /**
   * Parameters are hot_restart_enabled
   */
  using HotRestartVersionCb = std::function<std::string(bool)>;

  // Constructor for mobile
  OptionsImplBase() = default;

  // Setters for option fields. These are not part of the Options interface.
  void setBaseId(uint64_t base_id) { base_id_ = base_id; };
  void setUseDynamicBaseId(bool use_dynamic_base_id) { use_dynamic_base_id_ = use_dynamic_base_id; }
  void setBaseIdPath(const std::string& base_id_path) { base_id_path_ = base_id_path; }
  void setConcurrency(uint32_t concurrency) { concurrency_ = concurrency; }
  void setConfigPath(const std::string& config_path) { config_path_ = config_path; }
  void setConfigProto(const envoy::config::bootstrap::v3::Bootstrap& config_proto) {
    *config_proto_ = config_proto;
  }
  void setConfigProto(std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap>&& config_proto) {
    config_proto_ = std::move(config_proto);
  }
  void setConfigYaml(const std::string& config_yaml) { config_yaml_ = config_yaml; }
  void setAdminAddressPath(const std::string& admin_address_path) {
    admin_address_path_ = admin_address_path;
  }
  void setLocalAddressIpVersion(Network::Address::IpVersion local_address_ip_version) {
    local_address_ip_version_ = local_address_ip_version;
  }
  void setDrainTime(std::chrono::seconds drain_time) { drain_time_ = drain_time; }
  void setParentShutdownTime(std::chrono::seconds parent_shutdown_time) {
    parent_shutdown_time_ = parent_shutdown_time;
  }
  void setDrainStrategy(Server::DrainStrategy drain_strategy) { drain_strategy_ = drain_strategy; }
  void setLogLevel(spdlog::level::level_enum log_level) { log_level_ = log_level; }
  absl::Status setLogLevel(absl::string_view log_level);
  void setLogFormat(const std::string& log_format) {
    log_format_ = log_format;
    log_format_set_ = true;
  }
  void setLogPath(const std::string& log_path) { log_path_ = log_path; }
  void setRestartEpoch(uint64_t restart_epoch) { restart_epoch_ = restart_epoch; }
  void setMode(Server::Mode mode) { mode_ = mode; }
  void setFileFlushIntervalMsec(std::chrono::milliseconds file_flush_interval_msec) {
    file_flush_interval_msec_ = file_flush_interval_msec;
  }
  void setServiceClusterName(const std::string& service_cluster) {
    service_cluster_ = service_cluster;
  }
  void setServiceNodeName(const std::string& service_node) { service_node_ = service_node; }
  void setServiceZone(const std::string& service_zone) { service_zone_ = service_zone; }
  void setHotRestartDisabled(bool hot_restart_disabled) {
    hot_restart_disabled_ = hot_restart_disabled;
  }
  void setSignalHandling(bool signal_handling_enabled) {
    signal_handling_enabled_ = signal_handling_enabled;
  }
  void setCpusetThreads(bool cpuset_threads_enabled) { cpuset_threads_ = cpuset_threads_enabled; }
  void setAllowUnknownFields(bool allow_unknown_static_fields) {
    allow_unknown_static_fields_ = allow_unknown_static_fields;
  }
  void setRejectUnknownFieldsDynamic(bool reject_unknown_dynamic_fields) {
    reject_unknown_dynamic_fields_ = reject_unknown_dynamic_fields;
  }
  void setIgnoreUnknownFieldsDynamic(bool ignore_unknown_dynamic_fields) {
    ignore_unknown_dynamic_fields_ = ignore_unknown_dynamic_fields;
  }

  void setSocketPath(const std::string& socket_path) { socket_path_ = socket_path; }

  void setSocketMode(mode_t socket_mode) { socket_mode_ = socket_mode; }

  void setStatsTags(const Stats::TagVector& stats_tags) { stats_tags_ = stats_tags; }

  // Server::Options
  uint64_t baseId() const override { return base_id_; }
  bool useDynamicBaseId() const override { return use_dynamic_base_id_; }
  const std::string& baseIdPath() const override { return base_id_path_; }
  uint32_t concurrency() const override { return concurrency_; }
  const std::string& configPath() const override { return config_path_; }
  const envoy::config::bootstrap::v3::Bootstrap& configProto() const override {
    return *config_proto_;
  }
  const std::string& configYaml() const override { return config_yaml_; }
  bool allowUnknownStaticFields() const override { return allow_unknown_static_fields_; }
  bool rejectUnknownDynamicFields() const override { return reject_unknown_dynamic_fields_; }
  bool ignoreUnknownDynamicFields() const override { return ignore_unknown_dynamic_fields_; }
  const std::string& adminAddressPath() const override { return admin_address_path_; }
  Network::Address::IpVersion localAddressIpVersion() const override {
    return local_address_ip_version_;
  }
  std::chrono::seconds drainTime() const override { return drain_time_; }
  std::chrono::seconds parentShutdownTime() const override { return parent_shutdown_time_; }
  Server::DrainStrategy drainStrategy() const override { return drain_strategy_; }

  spdlog::level::level_enum logLevel() const override { return log_level_; }
  const std::vector<std::pair<std::string, spdlog::level::level_enum>>&
  componentLogLevels() const override {
    return component_log_levels_;
  }
  const std::string& logFormat() const override { return log_format_; }
  bool logFormatSet() const override { return log_format_set_; }
  bool logFormatEscaped() const override { return log_format_escaped_; }
  bool enableFineGrainLogging() const override { return enable_fine_grain_logging_; }
  const std::string& logPath() const override { return log_path_; }
  uint64_t restartEpoch() const override { return restart_epoch_; }
  Server::Mode mode() const override { return mode_; }
  std::chrono::milliseconds fileFlushIntervalMsec() const override {
    return file_flush_interval_msec_;
  }
  const std::string& serviceClusterName() const override { return service_cluster_; }
  const std::string& serviceNodeName() const override { return service_node_; }
  const std::string& serviceZone() const override { return service_zone_; }
  bool hotRestartDisabled() const override { return hot_restart_disabled_; }
  bool signalHandlingEnabled() const override { return signal_handling_enabled_; }
  bool mutexTracingEnabled() const override { return mutex_tracing_enabled_; }
  bool coreDumpEnabled() const override { return core_dump_enabled_; }
  const Stats::TagVector& statsTags() const override { return stats_tags_; }
  bool cpusetThreadsEnabled() const override { return cpuset_threads_; }
  const std::vector<std::string>& disabledExtensions() const override {
    return disabled_extensions_;
  }
  uint32_t count() const;
  const std::string& socketPath() const override { return socket_path_; }
  mode_t socketMode() const override { return socket_mode_; }
  // implemented by OptionsImpl
  Server::CommandLineOptionsPtr toCommandLineOptions() const override { return nullptr; }

  /**
   * disableExtensions parses the given set of extension names of
   * the form $CATEGORY/$NAME, and disables the corresponding extension
   * factories.
   */
  static void disableExtensions(const std::vector<std::string>&);

  /**
   * Parses and validates the provided log_level, returning the corresponding
   * spdlog::level::level_enum or an error status if the provided string is not a valid spdlog
   * string.
   */
  static absl::StatusOr<spdlog::level::level_enum>
  parseAndValidateLogLevel(absl::string_view log_level);

private:
  friend class OptionsImpl;

  uint64_t base_id_{0};
  bool use_dynamic_base_id_{false};
  std::string base_id_path_;
  uint32_t concurrency_{1};
  std::string config_path_;
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> config_proto_{
      new envoy::config::bootstrap::v3::Bootstrap()};
  std::string config_yaml_;
  bool allow_unknown_static_fields_{false};
  bool reject_unknown_dynamic_fields_{false};
  bool ignore_unknown_dynamic_fields_{false};
  std::string admin_address_path_;
  Network::Address::IpVersion local_address_ip_version_{Network::Address::IpVersion::v4};
  spdlog::level::level_enum log_level_{spdlog::level::info};
  std::vector<std::pair<std::string, spdlog::level::level_enum>> component_log_levels_;
  std::string component_log_level_str_;
  std::string log_format_{Logger::Logger::DEFAULT_LOG_FORMAT};
  bool log_format_set_{false};
  bool log_format_escaped_{false};
  std::string log_path_;
  uint64_t restart_epoch_{0};
  std::string service_cluster_;
  std::string service_node_;
  std::string service_zone_;
  std::chrono::milliseconds file_flush_interval_msec_{10000};
  std::chrono::seconds drain_time_{600};
  std::chrono::seconds parent_shutdown_time_{900};
  Server::DrainStrategy drain_strategy_{Server::DrainStrategy::Gradual};
  Server::Mode mode_{Server::Mode::Serve};
  bool hot_restart_disabled_{false};
  bool signal_handling_enabled_{true};
  bool mutex_tracing_enabled_{false};
  bool core_dump_enabled_{false};
  bool cpuset_threads_{false};
  std::vector<std::string> disabled_extensions_;
  Stats::TagVector stats_tags_;
  uint32_t count_{0};

  // Initialization added here to avoid integration_admin_test failure caused by uninitialized
  // enable_fine_grain_logging_.
  bool enable_fine_grain_logging_ = false;
  std::string socket_path_{"@envoy_domain_socket"};
  mode_t socket_mode_{0};
};

} // namespace Envoy
