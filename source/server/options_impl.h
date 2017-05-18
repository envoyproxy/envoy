#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/server/options.h"

#include "spdlog/spdlog.h"

namespace Envoy {
/**
 * Implementation of Server::Options.
 */
class OptionsImpl : public Server::Options {
public:
  OptionsImpl(int argc, char** argv, const std::string& hot_restart_version,
              spdlog::level::level_enum default_log_level);

  const std::string& serviceClusterName() { return service_cluster_; }
  const std::string& serviceNodeName() { return service_node_; }
  const std::string& serviceZone() { return service_zone_; }

  // Server::Options
  uint64_t baseId() override { return base_id_; }
  uint32_t concurrency() override { return concurrency_; }
  const std::string& configPath() override { return config_path_; }
  const std::string& adminAddressPath() override { return admin_address_path_; }
  std::chrono::seconds drainTime() override { return drain_time_; }
  spdlog::level::level_enum logLevel() override { return log_level_; }
  std::chrono::seconds parentShutdownTime() override { return parent_shutdown_time_; }
  uint64_t restartEpoch() override { return restart_epoch_; }
  Server::Mode mode() const override { return mode_; }
  std::chrono::milliseconds fileFlushIntervalMsec() override { return file_flush_interval_msec_; }

private:
  uint64_t base_id_;
  uint32_t concurrency_;
  std::string config_path_;
  std::string admin_address_path_;
  spdlog::level::level_enum log_level_;
  uint64_t restart_epoch_;
  std::string service_cluster_;
  std::string service_node_;
  std::string service_zone_;
  std::chrono::milliseconds file_flush_interval_msec_;
  std::chrono::seconds drain_time_;
  std::chrono::seconds parent_shutdown_time_;
  Server::Mode mode_;
};
} // Envoy
