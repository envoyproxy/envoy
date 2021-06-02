#include <cstdlib>

#include "envoy/common/exception.h"

#include "source/extensions/common/wasm/plugin.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace {

TEST(TestWasmConfig, Basic) {
  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  const std::string name = "my-plugin";
  plugin_config.set_name(name);

  const std::string function = "function";
  plugin_config.mutable_capability_restriction_config()->mutable_allowed_capabilities()->insert(
      {function, envoy::extensions::wasm::v3::SanitizationConfig()});

  auto proto_envs = plugin_config.mutable_vm_config()->mutable_environment_variables();
  const std::string host_env_key = "HOST_KEY";
  const std::string host_env_value = "HOST_VALUE";
  const std::string key = "KEY";
  const std::string value = "VALUE";
  TestEnvironment::setEnvVar(host_env_key, host_env_value, 0);
  proto_envs->mutable_host_env_keys()->Add(host_env_key.c_str());
  (*proto_envs->mutable_key_values())[key] = value;

  auto wasm_config = WasmConfig(plugin_config);
  EXPECT_EQ(name, wasm_config.config().name());
  auto allowed_capabilities = wasm_config.allowedCapabilities();
  EXPECT_NE(allowed_capabilities.find(function), allowed_capabilities.end());
  auto envs = wasm_config.environmentVariables();
  EXPECT_EQ(envs[host_env_key], host_env_value);
  EXPECT_EQ(envs[key], value);
}

TEST(TestWasmConfig, EnvKeyException) {
  {
    // Duplication in host_env_keys.
    envoy::extensions::wasm::v3::PluginConfig plugin_config;
    plugin_config.set_name("foo-wasm");
    auto proto_envs = plugin_config.mutable_vm_config()->mutable_environment_variables();
    auto key = "KEY";
    proto_envs->mutable_host_env_keys()->Add(key);
    proto_envs->mutable_host_env_keys()->Add(key);
    EXPECT_THROW_WITH_MESSAGE(
        WasmConfig config(plugin_config), EnvoyException,
        "Key KEY is duplicated in envoy.extensions.wasm.v3.VmConfig.environment_variables for "
        "foo-wasm. All the keys must be unique.");
  }
  {
    // Duplication between host_env_keys and key_values.
    envoy::extensions::wasm::v3::PluginConfig plugin_config;
    plugin_config.set_name("bar-wasm");
    auto proto_envs = plugin_config.mutable_vm_config()->mutable_environment_variables();
    auto key = "KEY";
    (*proto_envs->mutable_key_values())[key] = "VALUE";
    proto_envs->mutable_host_env_keys()->Add(key);
    EXPECT_THROW_WITH_MESSAGE(
        WasmConfig config(plugin_config), EnvoyException,
        "Key KEY is duplicated in envoy.extensions.wasm.v3.VmConfig.environment_variables for "
        "bar-wasm. All the keys must be unique.");
  }
}

TEST(TestWasmConfig, NullVMEnv) {
  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  plugin_config.mutable_vm_config()->set_runtime("envoy.wasm.runtime.null");
  (*plugin_config.mutable_vm_config()
        ->mutable_environment_variables()
        ->mutable_key_values())["key"] = "value";

  EXPECT_THROW_WITH_MESSAGE(
      WasmConfig config(plugin_config), EnvoyException,
      "envoy.extensions.wasm.v3.VmConfig.EnvironmentVariables.key_values must "
      "not be set for NullVm.");
}

} // namespace
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
