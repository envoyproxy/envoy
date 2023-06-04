#include "source/extensions/common/wasm/plugin.h"

#include "envoy/common/exception.h"

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
    const auto& envs = config_.vm_config().environment_variables();

    // We reject NullVm with key_values configuration
    // since it directly accesses Envoy's env vars and we should not modify Envoy's env vars here.
    // TODO(mathetake): Once proxy_get_map_values(type::EnvironmentVariables, ..) call is supported,
    // then remove this restriction.
    if (config.vm_config().runtime() == "envoy.wasm.runtime.null" && !envs.key_values().empty()) {
      throw EnvoyException("envoy.extensions.wasm.v3.VmConfig.EnvironmentVariables.key_values must "
                           "not be set for NullVm.");
    }

    // Check key duplication.
    absl::flat_hash_set<std::string> keys;
    for (const auto& env : envs.key_values()) {
      keys.insert(env.first);
    }
    for (const auto& key : envs.host_env_keys()) {
      if (!keys.insert(key).second) {
        throw EnvoyException(
            fmt::format("Key {} is duplicated in "
                        "envoy.extensions.wasm.v3.VmConfig.environment_variables for {}. "
                        "All the keys must be unique.",
                        key, config_.name()));
      }
    }

    // Construct merged key-value pairs.
    for (const auto& env : envs.key_values()) {
      envs_[env.first] = env.second;
    }
    for (const auto& key : envs.host_env_keys()) {
      if (auto value = std::getenv(key.data())) {
        envs_[key] = value;
      }
    }
  }

  if (config.vm_config().runtime() == "envoy.wasm.runtime.null" &&
      !config_.vm_config().log_destination().empty()) {
    throw EnvoyException("envoy.extensions.wasm.v3.VmConfig.log_destination must "
                         "not be set for NullVm.");
  }
  // Check key duplication.
  absl::flat_hash_set<std::string> keys;
  for (const auto& ld : config_.vm_config().log_destination()) {
    if (!keys.insert(ld.first).second) {
      throw EnvoyException(fmt::format("Key {} is duplicated in "
                                       "envoy.extensions.wasm.v3.VmConfig.log_destination for {}. "
                                       "All the keys must be unique.",
                                       ld.first, config_.name()));
    }
  }
  // Construct merged key-value pairs. Also check for boundary conditions
  // (e.g. empty file path).
  for (const auto& ld : config_.vm_config().log_destination()) {
    if (ld.second.file_path().empty()) {
      throw EnvoyException(
          fmt::format("Key {} value envoy.extensions.wasm.v3.VmConfig.LogDestination.file_path "
                      "must not be empty for {}.",
                      ld.first, config_.name()));
    }
    log_destinations_[ld.first] = ld.second.file_path();
  }
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
