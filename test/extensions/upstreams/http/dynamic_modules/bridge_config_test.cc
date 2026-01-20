#include "source/extensions/upstreams/http/dynamic_modules/bridge_config.h"

#include "test/extensions/dynamic_modules/util.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {

TEST(DynamicModuleHttpTcpBridgeConfigTest, CreateSuccess) {
  auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
      Envoy::Extensions::DynamicModules::testSharedObjectPath("http_tcp_bridge_no_op", "c"), false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto config = DynamicModuleHttpTcpBridgeConfig::create("test_bridge", "test_config",
                                                         std::move(dynamic_module.value()));
  ASSERT_TRUE(config.ok()) << config.status().message();
  EXPECT_NE(config.value(), nullptr);
  EXPECT_NE(config.value()->in_module_config_, nullptr);
}

TEST(DynamicModuleHttpTcpBridgeConfigTest, CreateFailureMissingSymbol) {
  // Use the no_op module which does not have the upstream bridge symbols.
  auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
      Envoy::Extensions::DynamicModules::testSharedObjectPath("no_op", "c"), false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto config = DynamicModuleHttpTcpBridgeConfig::create("test_bridge", "test_config",
                                                         std::move(dynamic_module.value()));
  EXPECT_FALSE(config.ok());
  EXPECT_THAT(config.status().message(),
              testing::HasSubstr("envoy_dynamic_module_on_upstream_http_tcp_bridge_config_new"));
}

TEST(DynamicModuleHttpTcpBridgeConfigTest, CreateFailureConfigNewReturnsNull) {
  auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
      Envoy::Extensions::DynamicModules::testSharedObjectPath("http_tcp_bridge_config_new_fail",
                                                              "c"),
      false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto config = DynamicModuleHttpTcpBridgeConfig::create("test_bridge", "test_config",
                                                         std::move(dynamic_module.value()));
  EXPECT_FALSE(config.ok());
  EXPECT_THAT(config.status().message(),
              testing::HasSubstr("failed to create in-module bridge configuration"));
}

TEST(DynamicModuleHttpTcpBridgeConfigTest, CreateWithEmptyConfig) {
  auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
      Envoy::Extensions::DynamicModules::testSharedObjectPath("http_tcp_bridge_no_op", "c"), false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  // Empty bridge name and config should still work with the no-op module.
  auto config = DynamicModuleHttpTcpBridgeConfig::create("", "", std::move(dynamic_module.value()));
  ASSERT_TRUE(config.ok()) << config.status().message();
  EXPECT_NE(config.value(), nullptr);
}

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
