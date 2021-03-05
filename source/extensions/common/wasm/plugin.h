#pragma once

#include <memory>

#include "envoy/extensions/wasm/v3/wasm.pb.validate.h"
#include "envoy/local_info/local_info.h"

#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "include/proxy-wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class WasmConfig {
public:
  WasmConfig(const envoy::extensions::wasm::v3::PluginConfig& config);
  const envoy::extensions::wasm::v3::PluginConfig& config() { return config_; }
  proxy_wasm::AllowedCapabilitiesMap& allowedCapabilities() { return allowed_capabilities_; }

private:
  const envoy::extensions::wasm::v3::PluginConfig& config_;
  proxy_wasm::AllowedCapabilitiesMap allowed_capabilities_{};
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
                   config.fail_open()),
        direction_(direction), local_info_(local_info), listener_metadata_(listener_metadata),
        wasm_config_(std::make_unique<WasmConfig>(config)) {}

  envoy::config::core::v3::TrafficDirection& direction() { return direction_; }
  const LocalInfo::LocalInfo& localInfo() { return local_info_; }
  const envoy::config::core::v3::Metadata* listenerMetadata() { return listener_metadata_; }
  WasmConfig& wasmConfig() { return *wasm_config_; }

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
