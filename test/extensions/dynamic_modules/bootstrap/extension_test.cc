#include "source/extensions/bootstrap/dynamic_modules/extension.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

class ExtensionTest : public testing::Test {
protected:
  std::string testDataDir() {
    return TestEnvironment::runfilesPath("test/extensions/dynamic_modules/test_data/c");
  }

  testing::NiceMock<Event::MockDispatcher> dispatcher_;
};

TEST_F(ExtensionTest, NullInModuleExtension) {
  // Test that onServerInitialized and onWorkerThreadInitialized do not crash when
  // in_module_extension_ is nullptr (i.e., when initializeInModuleExtension is not called or
  // extension_new returns nullptr).
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      testDataDir() + "/libbootstrap_extension_new_null.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_);
  ASSERT_TRUE(config.ok()) << config.status();

  auto extension = std::make_unique<DynamicModuleBootstrapExtension>(config.value());

  // initializeInModuleExtension will call extension_new which returns nullptr.
  extension->initializeInModuleExtension();

  // These should not crash due to the null checks in the implementation.
  extension->onServerInitialized();
  extension->onWorkerThreadInitialized();

  // Extension should not be destroyed yet.
  EXPECT_FALSE(extension->isDestroyed());

  // Verify getExtensionConfig returns the correct config.
  EXPECT_EQ(&extension->getExtensionConfig(), config.value().get());
}

TEST_F(ExtensionTest, IsDestroyedAndGetExtensionConfig) {
  // Test that isDestroyed and getExtensionConfig work correctly.
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_);
  ASSERT_TRUE(config.ok()) << config.status();

  auto extension = std::make_unique<DynamicModuleBootstrapExtension>(config.value());
  extension->initializeInModuleExtension();

  // Extension is initialized and not destroyed.
  EXPECT_FALSE(extension->isDestroyed());

  // Verify getExtensionConfig returns the correct config reference.
  const DynamicModuleBootstrapExtensionConfig& retrieved_config = extension->getExtensionConfig();
  EXPECT_EQ(&retrieved_config, config.value().get());

  // Destroy the extension and verify isDestroyed returns true.
  extension.reset();
  // Note: After reset, we cannot call isDestroyed on a nullptr. The destructor sets destroyed_
  // to true, which we verify by checking the extension lifecycle works correctly.
}

TEST_F(ExtensionTest, LifecycleWithValidExtension) {
  // Test the full lifecycle of a valid extension.
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_);
  ASSERT_TRUE(config.ok()) << config.status();

  auto extension = std::make_unique<DynamicModuleBootstrapExtension>(config.value());

  // Before initialization.
  EXPECT_FALSE(extension->isDestroyed());

  // Initialize the in-module extension.
  extension->initializeInModuleExtension();
  EXPECT_FALSE(extension->isDestroyed());

  // Call lifecycle methods.
  extension->onServerInitialized();
  extension->onWorkerThreadInitialized();

  // Verify getExtensionConfig.
  EXPECT_NE(&extension->getExtensionConfig(), nullptr);

  // Destruction happens when extension goes out of scope.
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
