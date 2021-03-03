#include "extensions/common/wasm/plugin.h"

#include <cstdlib>
#include <memory>

#include "envoy/common/exception.h"
#include "envoy/extensions/wasm/v3/wasm.pb.validate.h"
#include "envoy/local_info/local_info.h"

#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "extensions/common/wasm//well_known_names.h"

#include "include/proxy-wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

void setEnvVar(const std::string& name, const std::string& value, int overwrite) {
#ifdef WIN32
  if (!overwrite) {
    size_t requiredSize;
    ::getenv_s(&requiredSize, nullptr, 0, name.c_str());
    if (requiredSize != 0) {
      return;
    }
  }
  ::_putenv_s(name.c_str(), value.c_str());
#else
  ::setenv(name.c_str(), value.c_str(), overwrite);
#endif
}

WasmConfig::WasmConfig(const envoy::extensions::wasm::v3::PluginConfig& config) : config_(config) {
  for (auto& capability : config_.capability_restriction_config().allowed_capabilities()) {
    // TODO(rapilado): Set the SanitizationConfig fields once sanitization is implemented.
    allowed_capabilities_[capability.first] = proxy_wasm::SanitizationConfig();
  }

  auto has_env_config = config_.vm_config().has_environment_variables();
  if (has_env_config && config.vm_config().runtime() == WasmRuntimeNames::get().Null) {
    // On NullVm, std::getenv directly accesses to the Envoy's env vars, so we need to set here to
    // be consistent with actual Wasm VMs.
    for (auto& env : config_.vm_config().environment_variables().key_values()) {
      setEnvVar(env.first, env.second, true);
    }
  } else if (has_env_config) {
#define DUPLICATED_ENV_KEY_EXCEPTION(key, name)                                                    \
  throw EnvoyException(                                                                            \
      fmt::format("Key {} is duplicated in "                                                       \
                  "envoy.extensions.wasm.v3.VmConfig.environment_variables for {}. "               \
                  "All the keys must be unique.",                                                  \
                  key, name))
    auto envs = config_.vm_config().environment_variables();
    for (auto& key : envs.host_env_keys()) {
      if (envs_.find(key) == envs_.end()) {
        auto value = std::getenv(key.data());
        if (value) {
          envs_[key] = value;
        }
      } else {
        DUPLICATED_ENV_KEY_EXCEPTION(key, config_.name());
      }
    }

    for (auto& env : envs.key_values()) {
      if (envs_.find(env.first) == envs_.end()) {
        envs_[env.first] = env.second;
      } else {
        DUPLICATED_ENV_KEY_EXCEPTION(env.first, config_.name());
      }
    }
#undef DUPLICATED_KEY_EXCEPTION
  }
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
