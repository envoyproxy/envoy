#include "extensions/common/wasm/plugin.h"

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

WasmConfig::WasmConfig(const envoy::extensions::wasm::v3::PluginConfig& config) : config_(config) {
  for (auto& capability : config_.capability_restriction_config().allowed_capabilities()) {
    // TODO(rapilado): Set the SanitizationConfig fields once sanitization is implemented.
    allowed_capabilities_[capability.first] = proxy_wasm::SanitizationConfig();
  }
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
