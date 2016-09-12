#pragma once

#include "envoy/server/options.h"

/**
 * Implementation of Server::Options.
 */
class OptionsImpl : public Server::Options {
public:
  OptionsImpl(int argc, char** argv, const std::string& hot_restart_version,
              spdlog::level::level_enum default_log_level);

  // Server::Options
  uint64_t baseId() { return base_id_; }
  uint32_t concurrency() override { return concurrency_; }
  const std::string& configPath() override { return config_path_; }
  uint64_t logLevel() override { return log_level_; }
  uint64_t restartEpoch() override { return restart_epoch_; }
  const std::string& serviceClusterName() override { return service_cluster_; }
  const std::string& serviceNodeName() override { return service_node_; }
  const std::string& serviceZone() override { return service_zone_; }
  std::chrono::milliseconds flushIntervalMsec() override { return flush_interval_msec_; }

private:
  uint64_t base_id_;
  uint32_t concurrency_;
  std::string config_path_;
  uint64_t log_level_;
  uint64_t restart_epoch_;
  std::string service_cluster_;
  std::string service_node_;
  std::string service_zone_;
  std::chrono::milliseconds flush_interval_msec_;
};
