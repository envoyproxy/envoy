#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/server/options.h"
#include "envoy/stats/stats_options.h"

#include "common/stats/stats_options_impl.h"

#include "spdlog/spdlog.h"

namespace Envoy {
/**
 * Implementation of Server::Options.
 */
class OptionsImpl : public Server::Options {
public:
  /**
   * Parameters are max_num_stats, max_stat_name_len, hot_restart_enabled
   */
  typedef std::function<std::string(uint64_t, uint64_t, bool)> HotRestartVersionCb;

  /**
   * @throw NoServingException if Envoy has already done everything specified by the argv (e.g.
   *        print the hot restart version) and it's time to exit without serving HTTP traffic. The
   *        caller should exit(0) after any necessary cleanup.
   * @throw MalformedArgvException if something is wrong with the arguments (invalid flag or flag
   *        value). The caller should call exit(1) after any necessary cleanup.
   */
  OptionsImpl(int argc, const char* const* argv, const HotRestartVersionCb& hot_restart_version_cb,
              spdlog::level::level_enum default_log_level);

  // Setters for option fields. These are not part of the Options interface.
  void setBaseId(uint64_t base_id) { base_id_ = base_id; };
  void setConcurrency(uint32_t concurrency) { concurrency_ = concurrency; }
  void setConfigPath(const std::string& config_path) { config_path_ = config_path; }
  void setConfigYaml(const std::string& config_yaml) { config_yaml_ = config_yaml; }
  void setAdminAddressPath(const std::string& admin_address_path) {
    admin_address_path_ = admin_address_path;
  }
  void setLocalAddressIpVersion(Network::Address::IpVersion local_address_ip_version) {
    local_address_ip_version_ = local_address_ip_version;
  }
  void setDrainTime(std::chrono::seconds drain_time) { drain_time_ = drain_time; }
  void setLogLevel(spdlog::level::level_enum log_level) { log_level_ = log_level; }
  void setLogFormat(const std::string& log_format) { log_format_ = log_format; }
  void setLogPath(const std::string& log_path) { log_path_ = log_path; }
  void setParentShutdownTime(std::chrono::seconds parent_shutdown_time) {
    parent_shutdown_time_ = parent_shutdown_time;
  }
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
  void setMaxStats(uint64_t max_stats) { max_stats_ = max_stats; }
  void setStatsOptions(Stats::StatsOptionsImpl stats_options) { stats_options_ = stats_options; }
  void setHotRestartDisabled(bool hot_restart_disabled) {
    hot_restart_disabled_ = hot_restart_disabled;
  }

  // Server::Options
  uint64_t baseId() const override { return base_id_; }
  uint32_t concurrency() const override { return concurrency_; }
  const std::string& configPath() const override { return config_path_; }
  const std::string& configYaml() const override { return config_yaml_; }
  bool v2ConfigOnly() const override { return true; }
  const std::string& adminAddressPath() const override { return admin_address_path_; }
  Network::Address::IpVersion localAddressIpVersion() const override {
    return local_address_ip_version_;
  }
  std::chrono::seconds drainTime() const override { return drain_time_; }
  spdlog::level::level_enum logLevel() const override { return log_level_; }
  const std::vector<std::pair<std::string, spdlog::level::level_enum>>&
  componentLogLevels() const override {
    return component_log_levels_;
  }
  const std::string& logFormat() const override { return log_format_; }
  const std::string& logPath() const override { return log_path_; }
  std::chrono::seconds parentShutdownTime() const override { return parent_shutdown_time_; }
  uint64_t restartEpoch() const override { return restart_epoch_; }
  Server::Mode mode() const override { return mode_; }
  std::chrono::milliseconds fileFlushIntervalMsec() const override {
    return file_flush_interval_msec_;
  }
  const std::string& serviceClusterName() const override { return service_cluster_; }
  const std::string& serviceNodeName() const override { return service_node_; }
  const std::string& serviceZone() const override { return service_zone_; }
  uint64_t maxStats() const override { return max_stats_; }
  const Stats::StatsOptions& statsOptions() const override { return stats_options_; }
  bool hotRestartDisabled() const override { return hot_restart_disabled_; }

private:
  void parseComponentLogLevels(const std::string& component_log_levels);
  void logError(const std::string& error) const;

  uint64_t base_id_;
  uint32_t concurrency_;
  std::string config_path_;
  std::string config_yaml_;
  std::string admin_address_path_;
  Network::Address::IpVersion local_address_ip_version_;
  spdlog::level::level_enum log_level_;
  std::vector<std::pair<std::string, spdlog::level::level_enum>> component_log_levels_;
  std::string log_format_;
  std::string log_path_;
  uint64_t restart_epoch_;
  std::string service_cluster_;
  std::string service_node_;
  std::string service_zone_;
  std::chrono::milliseconds file_flush_interval_msec_;
  std::chrono::seconds drain_time_;
  std::chrono::seconds parent_shutdown_time_;
  Server::Mode mode_;
  uint64_t max_stats_;
  Stats::StatsOptionsImpl stats_options_;
  bool hot_restart_disabled_;

  friend class OptionsImplTest;
};

/**
 * Thrown when an OptionsImpl was not constructed because all of Envoy's work is done (for example,
 * it was started with --help and it's already printed a help message) so all that's left to do is
 * exit successfully.
 */
class NoServingException : public EnvoyException {
public:
  NoServingException() : EnvoyException("NoServingException") {}
};

/**
 * Thrown when an OptionsImpl was not constructed because the argv was invalid.
 */
class MalformedArgvException : public EnvoyException {
public:
  MalformedArgvException(const std::string& what) : EnvoyException(what) {}
};

} // namespace Envoy
