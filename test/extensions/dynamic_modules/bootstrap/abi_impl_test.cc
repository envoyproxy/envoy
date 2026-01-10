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
  envoy_dynamic_module_type_module_buffer message = {"trace message", 13};
  envoy_dynamic_module_callback_bootstrap_extension_log(
      nullptr, envoy_dynamic_module_type_log_level_Trace, message);
}

TEST_F(AbiImplTest, LogCallback_LevelDebug) {
  envoy_dynamic_module_type_module_buffer message = {"debug message", 13};
  envoy_dynamic_module_callback_bootstrap_extension_log(
      nullptr, envoy_dynamic_module_type_log_level_Debug, message);
}

TEST_F(AbiImplTest, LogCallback_LevelInfo) {
  envoy_dynamic_module_type_module_buffer message = {"info message", 12};
  envoy_dynamic_module_callback_bootstrap_extension_log(
      nullptr, envoy_dynamic_module_type_log_level_Info, message);
}

TEST_F(AbiImplTest, LogCallback_LevelWarn) {
  envoy_dynamic_module_type_module_buffer message = {"warn message", 12};
  envoy_dynamic_module_callback_bootstrap_extension_log(
      nullptr, envoy_dynamic_module_type_log_level_Warn, message);
}

TEST_F(AbiImplTest, LogCallback_LevelError) {
  envoy_dynamic_module_type_module_buffer message = {"error message", 13};
  envoy_dynamic_module_callback_bootstrap_extension_log(
      nullptr, envoy_dynamic_module_type_log_level_Error, message);
}

TEST_F(AbiImplTest, LogCallback_LevelCritical) {
  envoy_dynamic_module_type_module_buffer message = {"critical message", 16};
  envoy_dynamic_module_callback_bootstrap_extension_log(
      nullptr, envoy_dynamic_module_type_log_level_Critical, message);
}

TEST_F(AbiImplTest, LogCallback_LevelDefault) {
  // Off level falls through to default (no logging).
  envoy_dynamic_module_type_module_buffer message = {"off message", 11};
  envoy_dynamic_module_callback_bootstrap_extension_log(
      nullptr, envoy_dynamic_module_type_log_level_Off, message);
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
