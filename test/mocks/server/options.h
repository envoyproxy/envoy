#pragma once

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/server/options.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {
class MockOptions : public Options {
public:
  MockOptions() : MockOptions(std::string()) {}
  MockOptions(const std::string& config_path);
  ~MockOptions() override;

  MOCK_METHOD(uint64_t, baseId, (), (const));
  MOCK_METHOD(bool, useDynamicBaseId, (), (const));
  MOCK_METHOD(bool, skipHotRestartOnNoParent, (), (const));
  MOCK_METHOD(bool, skipHotRestartParentStats, (), (const));
  MOCK_METHOD(const std::string&, baseIdPath, (), (const));
  MOCK_METHOD(uint32_t, concurrency, (), (const));
  MOCK_METHOD(const std::string&, configPath, (), (const));
  MOCK_METHOD(const envoy::config::bootstrap::v3::Bootstrap&, configProto, (), (const));
  MOCK_METHOD(const std::string&, configYaml, (), (const));
  MOCK_METHOD(bool, allowUnknownStaticFields, (), (const));
  MOCK_METHOD(bool, rejectUnknownDynamicFields, (), (const));
  MOCK_METHOD(bool, ignoreUnknownDynamicFields, (), (const));
  MOCK_METHOD(const std::string&, adminAddressPath, (), (const));
  MOCK_METHOD(Network::Address::IpVersion, localAddressIpVersion, (), (const));
  MOCK_METHOD(std::chrono::seconds, drainTime, (), (const));
  MOCK_METHOD(std::chrono::seconds, parentShutdownTime, (), (const));
  MOCK_METHOD(Server::DrainStrategy, drainStrategy, (), (const));
  MOCK_METHOD(spdlog::level::level_enum, logLevel, (), (const));
  MOCK_METHOD((const std::vector<std::pair<std::string, spdlog::level::level_enum>>&),
              componentLogLevels, (), (const));
  MOCK_METHOD(const std::string&, logFormat, (), (const));
  MOCK_METHOD(bool, logFormatSet, (), (const));
  MOCK_METHOD(bool, logFormatEscaped, (), (const));
  MOCK_METHOD(bool, enableFineGrainLogging, (), (const));
  MOCK_METHOD(const std::string&, logPath, (), (const));
  MOCK_METHOD(uint64_t, restartEpoch, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, fileFlushIntervalMsec, (), (const));
  MOCK_METHOD(Mode, mode, (), (const));
  MOCK_METHOD(const std::string&, serviceClusterName, (), (const));
  MOCK_METHOD(const std::string&, serviceNodeName, (), (const));
  MOCK_METHOD(const std::string&, serviceZone, (), (const));
  MOCK_METHOD(bool, hotRestartDisabled, (), (const));
  MOCK_METHOD(bool, signalHandlingEnabled, (), (const));
  MOCK_METHOD(bool, mutexTracingEnabled, (), (const));
  MOCK_METHOD(bool, coreDumpEnabled, (), (const));
  MOCK_METHOD(bool, cpusetThreadsEnabled, (), (const));
  MOCK_METHOD(const std::vector<std::string>&, disabledExtensions, (), (const));
  MOCK_METHOD(Server::CommandLineOptionsPtr, toCommandLineOptions, (), (const));
  MOCK_METHOD(const std::string&, socketPath, (), (const));
  MOCK_METHOD(mode_t, socketMode, (), (const));
  MOCK_METHOD((const Stats::TagVector&), statsTags, (), (const));

  std::string config_path_;
  envoy::config::bootstrap::v3::Bootstrap config_proto_;
  std::string config_yaml_;
  absl::optional<uint32_t> bootstrap_version_;
  bool allow_unknown_static_fields_{};
  bool reject_unknown_dynamic_fields_{};
  bool ignore_unknown_dynamic_fields_{};
  std::string admin_address_path_;
  std::string service_cluster_name_;
  std::string service_node_name_;
  std::string service_zone_name_;
  spdlog::level::level_enum log_level_{spdlog::level::trace};
  std::string log_path_;
  uint32_t concurrency_{1};
  uint64_t hot_restart_epoch_{};
  bool hot_restart_disabled_{};
  bool signal_handling_enabled_{true};
  bool mutex_tracing_enabled_{};
  bool core_dump_enabled_{};
  bool cpuset_threads_enabled_{};
  std::vector<std::string> disabled_extensions_;
  std::string socket_path_;
  mode_t socket_mode_;
  Stats::TagVector stats_tags_;
};
} // namespace Server
} // namespace Envoy
