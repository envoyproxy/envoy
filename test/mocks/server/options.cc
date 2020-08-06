#include "options.h"

#include "envoy/admin/v3/server_info.pb.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::Invoke;
using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::ReturnRef;

MockOptions::MockOptions(const std::string& config_path) : config_path_(config_path) {
  ON_CALL(*this, concurrency()).WillByDefault(ReturnPointee(&concurrency_));
  ON_CALL(*this, configPath()).WillByDefault(ReturnRef(config_path_));
  ON_CALL(*this, configProto()).WillByDefault(ReturnRef(config_proto_));
  ON_CALL(*this, configYaml()).WillByDefault(ReturnRef(config_yaml_));
  ON_CALL(*this, bootstrapVersion()).WillByDefault(ReturnRef(bootstrap_version_));
  ON_CALL(*this, allowUnknownStaticFields()).WillByDefault(Invoke([this] {
    return allow_unknown_static_fields_;
  }));
  ON_CALL(*this, rejectUnknownDynamicFields()).WillByDefault(Invoke([this] {
    return reject_unknown_dynamic_fields_;
  }));
  ON_CALL(*this, ignoreUnknownDynamicFields()).WillByDefault(Invoke([this] {
    return ignore_unknown_dynamic_fields_;
  }));
  ON_CALL(*this, adminAddressPath()).WillByDefault(ReturnRef(admin_address_path_));
  ON_CALL(*this, serviceClusterName()).WillByDefault(ReturnRef(service_cluster_name_));
  ON_CALL(*this, serviceNodeName()).WillByDefault(ReturnRef(service_node_name_));
  ON_CALL(*this, serviceZone()).WillByDefault(ReturnRef(service_zone_name_));
  ON_CALL(*this, logLevel()).WillByDefault(Return(log_level_));
  ON_CALL(*this, logPath()).WillByDefault(ReturnRef(log_path_));
  ON_CALL(*this, restartEpoch()).WillByDefault(ReturnPointee(&hot_restart_epoch_));
  ON_CALL(*this, hotRestartDisabled()).WillByDefault(ReturnPointee(&hot_restart_disabled_));
  ON_CALL(*this, signalHandlingEnabled()).WillByDefault(ReturnPointee(&signal_handling_enabled_));
  ON_CALL(*this, mutexTracingEnabled()).WillByDefault(ReturnPointee(&mutex_tracing_enabled_));
  ON_CALL(*this, cpusetThreadsEnabled()).WillByDefault(ReturnPointee(&cpuset_threads_enabled_));
  ON_CALL(*this, disabledExtensions()).WillByDefault(ReturnRef(disabled_extensions_));
  ON_CALL(*this, toCommandLineOptions()).WillByDefault(Invoke([] {
    return std::make_unique<envoy::admin::v3::CommandLineOptions>();
  }));
}

MockOptions::~MockOptions() = default;

} // namespace Server
} // namespace Envoy
