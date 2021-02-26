#pragma once

#include <memory>

#include "envoy/extensions/wasm/v3/wasm.pb.validate.h"

#include "common/common/logger.h"

#include "absl/strings/str_cat.h"
#include "include/proxy-wasm/wasm.h"
#include "include/proxy-wasm/wasm_vm.h"
#include "include/proxy-wasm/word.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class WasmBaseConfig {
public:
  WasmBaseConfig(const envoy::extensions::wasm::v3::PluginConfig& config);
  const envoy::extensions::wasm::v3::PluginConfig& config() { return config_; }
  proxy_wasm::AllowedCapabilitiesMap& allowedCapabilitiesMap() { return allowed_capabilities_map_; }

private:
  const envoy::extensions::wasm::v3::PluginConfig& config_;
  proxy_wasm::AllowedCapabilitiesMap allowed_capabilities_map_{};
};

using WasmBaseConfigPtr = std::unique_ptr<WasmBaseConfig>;

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
