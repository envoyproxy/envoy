#pragma once

#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/wasm/v3/wasm.pb.validate.h"
#include "envoy/local_info/local_info.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "include/proxy-wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

// clang-format off
using EnvironmentVariableMap = std::unordered_map<std::string, std::string>;
// clang-format on
using FailurePolicy = envoy::extensions::wasm::v3::FailurePolicy;

class WasmConfig {
public:
  WasmConfig(const envoy::extensions::wasm::v3::PluginConfig& config);
  const envoy::extensions::wasm::v3::PluginConfig& config() { return config_; }
  proxy_wasm::AllowedCapabilitiesMap& allowedCapabilities() { return allowed_capabilities_; }
  EnvironmentVariableMap& environmentVariables() { return envs_; }

private:
  const envoy::extensions::wasm::v3::PluginConfig config_;
  proxy_wasm::AllowedCapabilitiesMap allowed_capabilities_{};
  EnvironmentVariableMap envs_;
};

using WasmConfigPtr = std::unique_ptr<WasmConfig>;

// Plugin contains the information for a filter/service.
class Plugin : public proxy_wasm::PluginBase {
public:
  Plugin(const envoy::extensions::wasm::v3::PluginConfig& config,
         envoy::config::core::v3::TrafficDirection direction,
         const LocalInfo::LocalInfo& local_info,
         const envoy::config::core::v3::Metadata* listener_metadata)
      : PluginBase(config.name(), config.root_id(), config.vm_config().vm_id(),
                   config.vm_config().runtime(), MessageUtil::anyToBytes(config.configuration()),
                   config.fail_open(), createPluginKey(config, direction, listener_metadata)),
        direction_(direction), failure_policy_(config.failure_policy()), local_info_(local_info),
        listener_metadata_(listener_metadata), wasm_config_(std::make_unique<WasmConfig>(config)) {

    if (config.fail_open()) {
      // If the legacy fail_open is set to true explicitly.

      // Only one of fail_open or failure_policy can be set explicitly.
      if (config.failure_policy() != FailurePolicy::UNSPECIFIED) {
        throw EnvoyException("only one of fail_open or failure_policy can be set");
      }

      // We treat fail_open as FAIL_IGNORE.
      failure_policy_ = FailurePolicy::FAIL_IGNORE;
    } else {
      // If the legacy fail_open is not set, we need to determine the failure policy.
      switch (config.failure_policy()) {
      case FailurePolicy::UNSPECIFIED: {
        const bool reload_by_default = Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.wasm_failure_reload_by_default");
        failure_policy_ =
            reload_by_default ? FailurePolicy::FAIL_RELOAD : FailurePolicy::FAIL_CLOSED;
        break;
      }
      case FailurePolicy::FAIL_RELOAD:
      case FailurePolicy::FAIL_CLOSED:
      case FailurePolicy::FAIL_IGNORE:
        // If the failure policy is FAIL_RELOAD, FAIL_CLOSED, or FAIL_IGNORE, we treat it as the
        // failure policy.
        failure_policy_ = config.failure_policy();
        break;
      default:
        throw EnvoyException("unknown failure policy");
      }
    }
    ASSERT(failure_policy_ == FailurePolicy::FAIL_CLOSED ||
           failure_policy_ == FailurePolicy::FAIL_IGNORE ||
           failure_policy_ == FailurePolicy::FAIL_RELOAD);
  }

  envoy::config::core::v3::TrafficDirection direction() { return direction_; }
  const LocalInfo::LocalInfo& localInfo() { return local_info_; }
  const envoy::config::core::v3::Metadata* listenerMetadata() { return listener_metadata_; }
  WasmConfig& wasmConfig() { return *wasm_config_; }
  FailurePolicy failurePolicy() const { return failure_policy_; }

private:
  static std::string createPluginKey(const envoy::extensions::wasm::v3::PluginConfig& config,
                                     envoy::config::core::v3::TrafficDirection direction,
                                     const envoy::config::core::v3::Metadata* listener_metadata) {
    return config.name() + "||" + envoy::config::core::v3::TrafficDirection_Name(direction) +
           (listener_metadata ? "||" + std::to_string(MessageUtil::hash(*listener_metadata)) : "");
  }

private:
  envoy::config::core::v3::TrafficDirection direction_;
  FailurePolicy failure_policy_;
  const LocalInfo::LocalInfo& local_info_;
  const envoy::config::core::v3::Metadata* listener_metadata_;
  WasmConfigPtr wasm_config_;
};

using PluginSharedPtr = std::shared_ptr<Plugin>;

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
