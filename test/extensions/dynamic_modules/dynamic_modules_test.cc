#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

TEST(DynamicModuleTestGeneral, InvalidPath) {
  absl::StatusOr<DynamicModulePtr> result = newDynamicModule("invalid_name", false);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

INSTANTIATE_TEST_SUITE_P(LanguageTests, DynamicModuleTestLanguages, testing::Values("c", "rust"),
                         DynamicModuleTestLanguages::languageParamToTestName);

TEST_P(DynamicModuleTestLanguages, DoNotClose) {
  std::string language = GetParam();
  using GetSomeVariableFuncType = int (*)(void);
  absl::StatusOr<DynamicModulePtr> module =
      newDynamicModule(testSharedObjectPath("no_op", language), false);
  EXPECT_TRUE(module.ok());
  const auto getSomeVariable =
      module->get()->getFunctionPointer<GetSomeVariableFuncType>("getSomeVariable");
  EXPECT_TRUE(getSomeVariable.ok());
  EXPECT_EQ(getSomeVariable.value()(), 1);
  EXPECT_EQ(getSomeVariable.value()(), 2);
  EXPECT_EQ(getSomeVariable.value()(), 3);

  // Release the module, and reload it.
  module->reset();
  module = newDynamicModule(testSharedObjectPath("no_op", language),
                            true); // This time, do not close the module.
  EXPECT_TRUE(module.ok());

  // This module must be reloaded and the variable must be reset.
  const auto getSomeVariable2 =
      (module->get()->getFunctionPointer<GetSomeVariableFuncType>("getSomeVariable"));
  EXPECT_TRUE(getSomeVariable2.ok());
  EXPECT_EQ(getSomeVariable2.value()(), 1); // Start from 1 again.
  EXPECT_EQ(getSomeVariable2.value()(), 2);
  EXPECT_EQ(getSomeVariable2.value()(), 3);

  // Release the module, and reload it.
  module->reset();
  module = newDynamicModule(testSharedObjectPath("no_op", language), false);
  EXPECT_TRUE(module.ok());

  // This module must be the already loaded one, and the variable must be kept.
  const auto getSomeVariable3 =
      module->get()->getFunctionPointer<GetSomeVariableFuncType>("getSomeVariable");
  EXPECT_TRUE(getSomeVariable3.ok());
  EXPECT_EQ(getSomeVariable3.value()(), 4); // Start from 4.
}

TEST(DynamicModuleTestLanguages, InitFunctionOnlyCalledOnce) {
  const auto path = testSharedObjectPath("program_init_assert", "c");
  absl::StatusOr<DynamicModulePtr> m1 = newDynamicModule(path, false);
  EXPECT_TRUE(m1.ok());
  // At this point, m1 is alive, so the init function should have been called.
  // When creating a new module with the same path, the init function should not be called again.
  absl::StatusOr<DynamicModulePtr> m2 = newDynamicModule(path, false);
  EXPECT_TRUE(m2.ok());
  m1->reset();
  m2->reset();

  // Even with the do_not_close=true, init function should only be called once.
  m1 = newDynamicModule(path, true);
  EXPECT_TRUE(m1.ok());
  m1->reset(); // Closing the module, but the module is still alive in the process.
  // This m2 should point to the same module as m1 whose handle is already freed, but
  // the init function should not be called again.
  m2 = newDynamicModule(path, true);
  EXPECT_TRUE(m2.ok());
}

TEST_P(DynamicModuleTestLanguages, NoProgramInit) {
  std::string language = GetParam();
  absl::StatusOr<DynamicModulePtr> result =
      newDynamicModule(testSharedObjectPath("no_program_init", language), false);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Failed to resolve symbol envoy_dynamic_module_on_program_init"));
}

TEST_P(DynamicModuleTestLanguages, ProgramInitFail) {
  std::string language = GetParam();
  absl::StatusOr<DynamicModulePtr> result =
      newDynamicModule(testSharedObjectPath("program_init_fail", language), false);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("Failed to initialize dynamic module:"));
}

TEST_P(DynamicModuleTestLanguages, ABIVersionMismatch) {
  std::string language = GetParam();
  absl::StatusOr<DynamicModulePtr> result =
      newDynamicModule(testSharedObjectPath("abi_version_mismatch", language), false);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr("ABI version mismatch: got invalid-version-hash, but expected"));
}

TEST(CreateDynamicModulesByName, OK) {
  TestEnvironment::setEnvVar(
      "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
      TestEnvironment::substitute(
          "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
      1);

  absl::StatusOr<DynamicModulePtr> module = newDynamicModuleByName("no_op", false);
  EXPECT_TRUE(module.ok());
  TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
}

TEST(CreateDynamicModulesByName, EnvVarNotSet) {
  // Without setting the search path, this should fail.
  absl::StatusOr<DynamicModulePtr> module = newDynamicModuleByName("no_op", false);
  EXPECT_FALSE(module.ok());
  EXPECT_EQ(module.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(module.status().message(),
              testing::HasSubstr("ENVOY_DYNAMIC_MODULES_SEARCH_PATH is not set"));
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
