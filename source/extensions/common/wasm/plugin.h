#pragma once

#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/wasm/v3/wasm.pb.validate.h"
#include "envoy/local_info/local_info.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "include/proxy-wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

// clang-format off
using EnvironmentVariableMap = std::unordered_map<std::string, std::string>;
// clang-format on

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
        direction_(direction), local_info_(local_info), listener_metadata_(listener_metadata),
        wasm_config_(std::make_unique<WasmConfig>(config)) {}

  envoy::config::core::v3::TrafficDirection& direction() { return direction_; }
  const LocalInfo::LocalInfo& localInfo() { return local_info_; }
  const envoy::config::core::v3::Metadata* listenerMetadata() { return listener_metadata_; }
  WasmConfig& wasmConfig() { return *wasm_config_; }

private:
  static std::string createPluginKey(const envoy::extensions::wasm::v3::PluginConfig& config,
                                     envoy::config::core::v3::TrafficDirection direction,
                                     const envoy::config::core::v3::Metadata* listener_metadata) {
    return config.name() + "||" + envoy::config::core::v3::TrafficDirection_Name(direction) +
           (listener_metadata ? "||" + std::to_string(MessageUtil::hash(*listener_metadata)) : "");
  }

private:
  envoy::config::core::v3::TrafficDirection direction_;
  const LocalInfo::LocalInfo& local_info_;
  const envoy::config::core::v3::Metadata* listener_metadata_;
  WasmConfigPtr wasm_config_;
};

using PluginSharedPtr = std::shared_ptr<Plugin>;

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
