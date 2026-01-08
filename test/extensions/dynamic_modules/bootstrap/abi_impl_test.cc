#include "source/extensions/dynamic_modules/abi.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

class AbiImplTest : public testing::Test {};

// Test all log levels to ensure complete coverage of the switch statement.
TEST_F(AbiImplTest, LogCallback_LevelTrace) {
  // Level 0 = trace.
  envoy_dynamic_module_callback_bootstrap_extension_log(nullptr, 0, "trace message", 13);
}

TEST_F(AbiImplTest, LogCallback_LevelDebug) {
  // Level 1 = debug.
  envoy_dynamic_module_callback_bootstrap_extension_log(nullptr, 1, "debug message", 13);
}

TEST_F(AbiImplTest, LogCallback_LevelInfo) {
  // Level 2 = info.
  envoy_dynamic_module_callback_bootstrap_extension_log(nullptr, 2, "info message", 12);
}

TEST_F(AbiImplTest, LogCallback_LevelWarn) {
  // Level 3 = warn.
  envoy_dynamic_module_callback_bootstrap_extension_log(nullptr, 3, "warn message", 12);
}

TEST_F(AbiImplTest, LogCallback_LevelError) {
  // Level 4 = error.
  envoy_dynamic_module_callback_bootstrap_extension_log(nullptr, 4, "error message", 13);
}

TEST_F(AbiImplTest, LogCallback_LevelCritical) {
  // Level 5 = critical.
  envoy_dynamic_module_callback_bootstrap_extension_log(nullptr, 5, "critical message", 16);
}

TEST_F(AbiImplTest, LogCallback_LevelDefault) {
  // Level 99 = unknown level, falls through to default (info).
  envoy_dynamic_module_callback_bootstrap_extension_log(nullptr, 99, "default message", 15);
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
