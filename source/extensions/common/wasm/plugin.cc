#include "source/extensions/common/wasm/plugin.h"

#include "envoy/common/exception.h"

#include "absl/strings/str_cat.h"
#include "include/proxy-wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

envoy::extensions::wasm::v3::PluginConfig
normalizeConfig(const envoy::extensions::wasm::v3::PluginConfig& config) {
  // The capability restrictions are applied when the Wasm VM is created and are shared by every
  // plugin running in it, so they belong to the VM configuration. The plugin level field is
  // deprecated in favor of the VM level one: move it there and clear it, so that the VM level field
  // is the only place the restrictions are ever read from.
  if (!config.has_capability_restriction_config()) {
    return config;
  }
  envoy::extensions::wasm::v3::PluginConfig normalized = config;
  // The VM level restrictions win when both are set.
  if (!config.vm_config().has_capability_restriction_config()) {
    *normalized.mutable_vm_config()->mutable_capability_restriction_config() =
        config.capability_restriction_config();
  }
  normalized.clear_capability_restriction_config();
  return normalized;
}

WasmConfig::WasmConfig(const envoy::extensions::wasm::v3::PluginConfig& config)
    : config_(normalizeConfig(config)) {
  for (auto& capability :
       config_.vm_config().capability_restriction_config().allowed_capabilities()) {
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
}

std::string Plugin::createPluginKey(const envoy::extensions::wasm::v3::PluginConfig& config) {
  // Every field of the plugin configuration is part of the plugin identity, so that distinct
  // configurations never share an instance and identical configurations always do (which keeps the
  // plugin reusable across xDS updates and bounds the number of root contexts). `vm_config` is
  // excluded because two plugins can only share a root context when they share a VM, and VM
  // identity is the VM key that proxy-wasm prepends to this key when caching thread-local plugins.
  // That key covers the fields of `vm_config` the VM is built from (see the `makeVmKey()` call in
  // wasm.cc), so between the two keys the only fields left out are the ones that affect neither the
  // VM nor the plugin, namely `allow_precompiled` and `nack_on_code_cache_miss`.
  //
  // The copy below only happens when a plugin is configured, and the plugin configuration is deep
  // copied by WasmConfig anyway.
  envoy::extensions::wasm::v3::PluginConfig key_config = config;
  key_config.clear_vm_config();
  return absl::StrCat(config.name(), "||", MessageUtil::hash(key_config));
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
