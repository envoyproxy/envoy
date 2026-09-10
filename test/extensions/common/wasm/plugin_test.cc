#include <cstdlib>

#include "envoy/common/exception.h"

#include "source/extensions/common/wasm/plugin.h"

#include "test/mocks/local_info/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Contains;
using testing::Key;
using testing::NiceMock;
using testing::Not;

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
  EXPECT_THAT(allowed_capabilities, Contains(Key(function)));
  auto envs = wasm_config.environmentVariables();
  EXPECT_EQ(envs[host_env_key], host_env_value);
  EXPECT_EQ(envs[key], value);
}

// The capability restrictions are read from the VM configuration.
TEST(TestWasmConfig, VmLevelCapabilityRestriction) {
  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  plugin_config.mutable_vm_config()
      ->mutable_capability_restriction_config()
      ->mutable_allowed_capabilities()
      ->insert({"proxy_log", envoy::extensions::wasm::v3::SanitizationConfig()});

  auto wasm_config = WasmConfig(plugin_config);
  EXPECT_THAT(wasm_config.allowedCapabilities(), Contains(Key("proxy_log")));
  EXPECT_THAT(
      wasm_config.config().vm_config().capability_restriction_config().allowed_capabilities(),
      Contains(Key("proxy_log")));
}

// The deprecated plugin level capability restrictions are moved into the VM configuration, leaving
// the VM level field as the only place the restrictions can be read from.
TEST(TestWasmConfig, DEPRECATED_FEATURE_TEST(DeprecatedPluginLevelCapabilityRestriction)) {
  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  plugin_config.mutable_capability_restriction_config()->mutable_allowed_capabilities()->insert(
      {"proxy_log", envoy::extensions::wasm::v3::SanitizationConfig()});

  auto wasm_config = WasmConfig(plugin_config);
  EXPECT_THAT(wasm_config.allowedCapabilities(), Contains(Key("proxy_log")));
  EXPECT_THAT(
      wasm_config.config().vm_config().capability_restriction_config().allowed_capabilities(),
      Contains(Key("proxy_log")));
  EXPECT_FALSE(wasm_config.config().has_capability_restriction_config());
}

// The VM level capability restrictions win when both are set.
TEST(TestWasmConfig, DEPRECATED_FEATURE_TEST(VmLevelCapabilityRestrictionWins)) {
  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  plugin_config.mutable_capability_restriction_config()->mutable_allowed_capabilities()->insert(
      {"proxy_log", envoy::extensions::wasm::v3::SanitizationConfig()});
  plugin_config.mutable_vm_config()
      ->mutable_capability_restriction_config()
      ->mutable_allowed_capabilities()
      ->insert({"proxy_on_vm_start", envoy::extensions::wasm::v3::SanitizationConfig()});

  auto wasm_config = WasmConfig(plugin_config);
  EXPECT_THAT(wasm_config.allowedCapabilities(), Contains(Key("proxy_on_vm_start")));
  EXPECT_THAT(wasm_config.allowedCapabilities(), Not(Contains(Key("proxy_log"))));
  EXPECT_FALSE(wasm_config.config().has_capability_restriction_config());
}

// A configuration without any capability restriction leaves the VM unrestricted, and no empty VM
// configuration is materialized.
TEST(TestWasmConfig, NoCapabilityRestriction) {
  envoy::extensions::wasm::v3::PluginConfig plugin_config;
  plugin_config.set_name("my-plugin");

  auto wasm_config = WasmConfig(plugin_config);
  EXPECT_TRUE(wasm_config.allowedCapabilities().empty());
  EXPECT_FALSE(wasm_config.config().has_vm_config());
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

class PluginKeyTest : public testing::Test {
protected:
  std::string key(const envoy::extensions::wasm::v3::PluginConfig& config) {
    return Plugin(config, local_info_).key();
  }

  envoy::extensions::wasm::v3::PluginConfig baseConfig() {
    envoy::extensions::wasm::v3::PluginConfig config;
    config.set_name("my-plugin");
    config.set_root_id("my-root");
    config.mutable_vm_config()->set_runtime("envoy.wasm.runtime.null");
    config.mutable_configuration()->set_value("plugin-configuration");
    return config;
  }

  NiceMock<LocalInfo::MockLocalInfo> local_info_;
};

// Any difference in the plugin configuration produces a distinct plugin identity, including the
// fields proxy-wasm does not hash into the plugin identity itself.
TEST_F(PluginKeyTest, ConfigDifferencesProduceDistinctKeys) {
  const auto config = baseConfig();
  const std::string base_key = key(config);

  {
    auto other = config;
    other.set_name("other-plugin");
    EXPECT_NE(base_key, key(other));
  }
  {
    auto other = config;
    other.set_root_id("other-root");
    EXPECT_NE(base_key, key(other));
  }
  {
    auto other = config;
    other.mutable_configuration()->set_value("other-configuration");
    EXPECT_NE(base_key, key(other));
  }
  {
    auto other = config;
    other.set_failure_policy(envoy::extensions::wasm::v3::FailurePolicy::FAIL_OPEN);
    EXPECT_NE(base_key, key(other));
  }
  {
    auto other = config;
    other.mutable_allow_on_headers_stop_iteration()->set_value(true);
    EXPECT_NE(base_key, key(other));
  }
  {
    auto other = config;
    other.mutable_reload_config()->mutable_backoff()->mutable_base_interval()->set_seconds(30);
    EXPECT_NE(base_key, key(other));
  }
}

// Same as above for the deprecated plugin level capability restrictions, which are part of the
// plugin configuration and so of the plugin identity.
TEST_F(PluginKeyTest, DEPRECATED_FEATURE_TEST(DeprecatedCapabilityRestrictionProducesDistinctKey)) {
  const auto config = baseConfig();
  auto other = config;
  other.mutable_capability_restriction_config()->mutable_allowed_capabilities()->insert(
      {"proxy_log", envoy::extensions::wasm::v3::SanitizationConfig()});
  EXPECT_NE(key(config), key(other));
}

// The VM configuration is not part of the plugin key: VM identity is covered by the VM key, which
// is prepended to the plugin key when caching thread-local plugins.
TEST_F(PluginKeyTest, VmConfigIsNotPartOfTheKey) {
  const auto config = baseConfig();
  auto other = config;
  other.mutable_vm_config()->set_vm_id("other-vm");
  other.mutable_vm_config()->mutable_code()->mutable_local()->set_inline_string("code");
  EXPECT_EQ(key(config), key(other));
}

} // namespace
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
