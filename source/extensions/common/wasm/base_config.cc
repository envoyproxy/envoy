#include "extensions/common/wasm/base_config.h"

#include <memory>

#include "envoy/extensions/wasm/v3/wasm.pb.validate.h"

#include "extensions/common/wasm/wasm.h"

#include "absl/strings/str_cat.h"
#include "include/proxy-wasm/wasm_vm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

WasmBaseConfig::WasmBaseConfig(const envoy::extensions::wasm::v3::PluginConfig& config)
    : config_(config) {
  for (auto& capability : config_.capability_restriction_config().allowed_capabilities()) {
    // TODO(rapilado): Set the SanitizationConfig fields once sanitization is implemented.
    allowed_capabilities_map_[capability.first] = proxy_wasm::SanitizationConfig();
  }
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
