#include "extensions/common/wasm/plugin.h"

#include "envoy/common/exception.h"

#include "extensions/common/wasm/well_known_names.h"

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

  if (config_.vm_config().has_environment_variables()) {
    auto envs = config_.vm_config().environment_variables();

    // We reject NullVm with environment_variables configuration
    // since it directly accesses Envoy's env vars.
    if (config.vm_config().runtime() == WasmRuntimeNames::get().Null &&
        (!envs.key_values().empty() || !envs.host_env_keys().empty())) {
      throw EnvoyException("Environment variables must not be set for NullVm.");
    }

    // Check key duplication.
    absl::flat_hash_set<std::string> keys;
    for (auto& env : envs.key_values()) {
      keys.insert(env.first);
    }
    for (auto& key : envs.host_env_keys()) {
      if (keys.find(key) == keys.end()) {
        keys.insert(key);
      } else {
        throw EnvoyException(
            fmt::format("Key {} is duplicated in "
                        "envoy.extensions.wasm.v3.VmConfig.environment_variables for {}. "
                        "All the keys must be unique.",
                        key, config_.name()));
      }
    }

    // Construct merged key-value pairs.
    for (auto& env : envs.key_values()) {
      envs_[env.first] = env.second;
    }
    for (auto& key : envs.host_env_keys()) {
      if (auto value = std::getenv(key.data())) {
        envs_[key] = value;
      }
    }
  }
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
