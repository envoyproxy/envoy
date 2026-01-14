#include "source/extensions/dynamic_modules/abi.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace {

// Test that the weak symbol stub for scheduler_new triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, SchedulerNewEnvoyBug) {
  EXPECT_ENVOY_BUG(
      { envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(nullptr); },
      "not implemented in this context");
}

// Test that the weak symbol stub for scheduler_delete triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, SchedulerDeleteEnvoyBug) {
  EXPECT_ENVOY_BUG(
      { envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(nullptr); },
      "not implemented in this context");
}

// Test that the weak symbol stub for scheduler_commit triggers an ENVOY_BUG when called.
TEST(CommonAbiImplTest, SchedulerCommitEnvoyBug) {
  EXPECT_ENVOY_BUG(
      { envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(nullptr, 0); },
      "not implemented in this context");
}

} // namespace
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
