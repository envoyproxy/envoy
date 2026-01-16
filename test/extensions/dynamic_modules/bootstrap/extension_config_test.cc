#include "source/extensions/bootstrap/dynamic_modules/extension_config.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

class ExtensionConfigTest : public testing::Test {
protected:
  std::string testDataDir() {
    return TestEnvironment::runfilesPath("test/extensions/dynamic_modules/test_data/c");
  }

  testing::NiceMock<Event::MockDispatcher> dispatcher_;
};

TEST_F(ExtensionConfigTest, LoadOK) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_);
  ASSERT_TRUE(config.ok()) << config.status();
  EXPECT_NE(config.value()->in_module_config_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_config_destroy_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_new_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_server_initialized_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_worker_thread_initialized_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_destroy_, nullptr);
  EXPECT_NE(config.value()->on_bootstrap_extension_config_scheduled_, nullptr);
}

TEST_F(ExtensionConfigTest, ConfigNewFail) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_config_new.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_);
  EXPECT_FALSE(config.ok());
  EXPECT_EQ(config.status().message(), "Failed to initialize dynamic module");
}

TEST_F(ExtensionConfigTest, MissingConfigDestroy) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_config_destroy.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_);
  EXPECT_FALSE(config.ok());
  EXPECT_THAT(config.status().message(),
              testing::HasSubstr("envoy_dynamic_module_on_bootstrap_extension_config_destroy"));
}

TEST_F(ExtensionConfigTest, MissingExtensionNew) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_extension_new.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_);
  EXPECT_FALSE(config.ok());
  EXPECT_THAT(config.status().message(),
              testing::HasSubstr("envoy_dynamic_module_on_bootstrap_extension_new"));
}

TEST_F(ExtensionConfigTest, MissingServerInitialized) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_server_initialized.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_);
  EXPECT_FALSE(config.ok());
  EXPECT_THAT(config.status().message(),
              testing::HasSubstr("envoy_dynamic_module_on_bootstrap_extension_server_initialized"));
}

TEST_F(ExtensionConfigTest, MissingWorkerThreadInitialized) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_worker_initialized.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_);
  EXPECT_FALSE(config.ok());
  EXPECT_THAT(
      config.status().message(),
      testing::HasSubstr("envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized"));
}

TEST_F(ExtensionConfigTest, MissingExtensionDestroy) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_extension_destroy.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_);
  EXPECT_FALSE(config.ok());
  EXPECT_THAT(config.status().message(),
              testing::HasSubstr("envoy_dynamic_module_on_bootstrap_extension_destroy"));
}

TEST_F(ExtensionConfigTest, MissingConstructor) {
  // Test that config creation fails when envoy_dynamic_module_on_bootstrap_extension_config_new
  // symbol is missing.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_constructor.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_);
  EXPECT_FALSE(config.ok());
  EXPECT_THAT(config.status().message(),
              testing::HasSubstr("envoy_dynamic_module_on_bootstrap_extension_config_new"));
}

TEST_F(ExtensionConfigTest, MissingConfigScheduled) {
  // Test that config creation fails when
  // envoy_dynamic_module_on_bootstrap_extension_config_scheduled symbol is missing.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_no_config_scheduled.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_);
  EXPECT_FALSE(config.ok());
  EXPECT_THAT(config.status().message(),
              testing::HasSubstr("envoy_dynamic_module_on_bootstrap_extension_config_scheduled"));
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
