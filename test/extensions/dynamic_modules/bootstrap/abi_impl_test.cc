#include "source/extensions/bootstrap/dynamic_modules/extension_config.h"
#include "source/extensions/dynamic_modules/abi.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

class BootstrapAbiImplTest : public testing::Test {
protected:
  std::string testDataDir() {
    return TestEnvironment::runfilesPath("test/extensions/dynamic_modules/test_data/c");
  }

  testing::NiceMock<Event::MockDispatcher> dispatcher_;
};

// Test that the scheduler can be created, used, and deleted.
TEST_F(BootstrapAbiImplTest, SchedulerLifecycle) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Create a scheduler via the ABI callback.
  auto* scheduler_ptr = envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(
      config.value()->thisAsVoidPtr());
  EXPECT_NE(scheduler_ptr, nullptr);

  // Delete the scheduler via the ABI callback.
  envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(scheduler_ptr);
}

// Test that the scheduler commit posts to the dispatcher.
TEST_F(BootstrapAbiImplTest, SchedulerCommit) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Create a scheduler via the ABI callback.
  auto* scheduler_ptr = envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(
      config.value()->thisAsVoidPtr());
  EXPECT_NE(scheduler_ptr, nullptr);

  // Expect the dispatcher to receive a post call when commit is called.
  Event::PostCb captured_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(testing::Invoke([&](Event::PostCb cb) {
    captured_cb = std::move(cb);
  }));

  // Commit an event via the ABI callback.
  envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(scheduler_ptr, 42);

  // Execute the callback to complete the flow.
  captured_cb();

  // Clean up.
  envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(scheduler_ptr);
}

// Test that onScheduled is called when the posted callback executes.
TEST_F(BootstrapAbiImplTest, OnScheduledCallback) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Create a scheduler via the ABI callback.
  auto* scheduler_ptr = envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(
      config.value()->thisAsVoidPtr());
  EXPECT_NE(scheduler_ptr, nullptr);

  // Capture the posted callback.
  Event::PostCb captured_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(testing::Invoke([&](Event::PostCb cb) {
    captured_cb = std::move(cb);
  }));

  // Commit an event via the ABI callback.
  envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(scheduler_ptr, 123);

  // Execute the captured callback to trigger onScheduled.
  captured_cb();

  // Clean up.
  envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(scheduler_ptr);
}

// Test that onScheduled handles the case when config is already destroyed.
TEST_F(BootstrapAbiImplTest, OnScheduledAfterConfigDestroyed) {
  Event::PostCb captured_cb;

  {
    auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
        testDataDir() + "/libbootstrap_no_op.so", false);
    ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

    auto config = newDynamicModuleBootstrapExtensionConfig(
        "test", "config", std::move(dynamic_module.value()), dispatcher_);
    ASSERT_TRUE(config.ok()) << config.status();

    // Create a scheduler via the ABI callback.
    auto* scheduler_ptr = envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(
        config.value()->thisAsVoidPtr());
    EXPECT_NE(scheduler_ptr, nullptr);

    // Capture the posted callback.
    EXPECT_CALL(dispatcher_, post(_)).WillOnce(testing::Invoke([&](Event::PostCb cb) {
      captured_cb = std::move(cb);
    }));

    // Commit an event via the ABI callback.
    envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(scheduler_ptr, 456);

    // Delete the scheduler before the callback is executed.
    envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(scheduler_ptr);

    // Config goes out of scope here and is destroyed.
  }

  // Execute the captured callback after config is destroyed.
  // This should not crash - the weak_ptr should be expired.
  captured_cb();
}

// Test calling onScheduled directly.
TEST_F(BootstrapAbiImplTest, OnScheduledDirect) {
  auto dynamic_module =
      Extensions::DynamicModules::newDynamicModule(testDataDir() + "/libbootstrap_no_op.so", false);
  ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status();

  auto config = newDynamicModuleBootstrapExtensionConfig(
      "test", "config", std::move(dynamic_module.value()), dispatcher_);
  ASSERT_TRUE(config.ok()) << config.status();

  // Call onScheduled directly - this should call the in-module hook.
  config.value()->onScheduled(789);
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
