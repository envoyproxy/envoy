#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/server/options.h"

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
  OptionsImpl(int argc, char** argv, const HotRestartVersionCb& hot_restart_version_cb,
              spdlog::level::level_enum default_log_level);

  // Server::Options
  uint64_t baseId() const override { return base_id_; }
  void setBaseId(uint64_t base_id) { base_id_ = base_id; };
  uint32_t concurrency() const override { return concurrency_; }
  void setConcurrency(uint32_t concurrency) { concurrency_ = concurrency; }
  const std::string& configPath() const override { return config_path_; }
  void setConfigPath(const std::string& config_path) { config_path_ = config_path; }
  const std::string& configYaml() const override { return config_yaml_; }
  void setConfigYaml(const std::string& config_yaml) { config_yaml_ = config_yaml; }
  bool v2ConfigOnly() const override { return v2_config_only_; }
  void setV2ConfigOnly(bool v2_config_only) { v2_config_only_ = v2_config_only; }
  const std::string& adminAddressPath() const override { return admin_address_path_; }
  void setAdminAddressPath(const std::string& admin_address_path) {
    admin_address_path_ = admin_address_path;
  }
  Network::Address::IpVersion localAddressIpVersion() const override {
    return local_address_ip_version_;
  }
  void setLocalAddressIpVersion(Network::Address::IpVersion local_address_ip_version) {
    local_address_ip_version_ = local_address_ip_version;
  }
  std::chrono::seconds drainTime() const override { return drain_time_; }
  void setDrainTime(std::chrono::seconds drain_time) { drain_time_ = drain_time; }
  spdlog::level::level_enum logLevel() const override { return log_level_; }
  void setLogLevel(spdlog::level::level_enum log_level) { log_level_ = log_level; }
  const std::string& logFormat() const override { return log_format_; }
  void setLogFormat(const std::string& log_format) { log_format_ = log_format; }
  const std::string& logPath() const override { return log_path_; }
  void setLogPath(const std::string& log_path) { log_path_ = log_path; }
  std::chrono::seconds parentShutdownTime() const override { return parent_shutdown_time_; }
  void setParentShutdownTime(std::chrono::seconds parent_shutdown_time) {
    parent_shutdown_time_ = parent_shutdown_time;
  }
  uint64_t restartEpoch() const override { return restart_epoch_; }
  void setRestartEpoch(uint64_t restart_epoch) { restart_epoch_ = restart_epoch; }
  Server::Mode mode() const override { return mode_; }
  void setMode(Server::Mode mode) { mode_ = mode; }
  std::chrono::milliseconds fileFlushIntervalMsec() const override {
    return file_flush_interval_msec_;
  }
  void setFileFlushIntervalMsec(std::chrono::milliseconds file_flush_interval_msec) {
    file_flush_interval_msec_ = file_flush_interval_msec;
  }
  const std::string& serviceClusterName() const override { return service_cluster_; }
  void setServiceClusterName(const std::string& service_cluster) {
    service_cluster_ = service_cluster;
  }
  const std::string& serviceNodeName() const override { return service_node_; }
  void setServiceNodeName(const std::string& service_node) { service_node_ = service_node; }
  const std::string& serviceZone() const override { return service_zone_; }
  void setServiceZone(const std::string& service_zone) { service_zone_ = service_zone; }
  uint64_t maxStats() const override { return max_stats_; }
  void setMaxStats(uint64_t max_stats) { max_stats_ = max_stats; }
  uint64_t maxObjNameLength() const override { return max_obj_name_length_; }
  void setMaxObjNameLength(uint64_t max_obj_name_length) {
    max_obj_name_length_ = max_obj_name_length;
  }
  bool hotRestartDisabled() const override { return hot_restart_disabled_; }
  void setHotRestartDisabled(bool hot_restart_disabled) {
    hot_restart_disabled_ = hot_restart_disabled;
  }

private:
  uint64_t base_id_;
  uint32_t concurrency_;
  std::string config_path_;
  std::string config_yaml_;
  bool v2_config_only_;
  std::string admin_address_path_;
  Network::Address::IpVersion local_address_ip_version_;
  spdlog::level::level_enum log_level_;
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
  uint64_t max_obj_name_length_;
  bool hot_restart_disabled_;
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
