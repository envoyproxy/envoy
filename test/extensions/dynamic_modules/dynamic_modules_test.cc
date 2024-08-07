#include <memory>

#include "envoy/common/exception.h"

#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

// This loads a shared object file from the test_data directory.
std::string testSharedObjectPath(std::string name, std::string language) {
  return TestEnvironment::substitute(
             "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/") +
         language + "/lib" + name + ".so";
}

TEST(DynamicModuleTestGeneral, InvalidPath) {
  absl::StatusOr<DynamicModuleSharedPtr> result = newDynamicModule("invalid_name", false);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

// This tests the behavior around do_not_close flag. Only implemented for C as Go cannot be
// reloaded.
TEST(DynamicModuleTestGeneral, DoNotClose) {
  using GetSomeVariableFuncType = int (*)();
  absl::StatusOr<DynamicModuleSharedPtr> module =
      newDynamicModule(testSharedObjectPath("no_op", "c"), false);
  EXPECT_TRUE(module.ok());
  const auto getSomeVariable =
      module->get()->getFunctionPointer<GetSomeVariableFuncType>("getSomeVariable");
  EXPECT_EQ(getSomeVariable(), 1);
  EXPECT_EQ(getSomeVariable(), 2);
  EXPECT_EQ(getSomeVariable(), 3);

  // Release the module, and reload it.
  module->reset();
  module = newDynamicModule(testSharedObjectPath("no_op", "c"),
                            true); // This time, do not close the module.
  EXPECT_TRUE(module.ok());

  // This module must be reloaded and the variable must be reset.
  const auto getSomeVariable2 =
      (module->get()->getFunctionPointer<GetSomeVariableFuncType>("getSomeVariable"));
  EXPECT_NE(getSomeVariable2, nullptr);
  EXPECT_EQ(getSomeVariable2(), 1); // Start from 1 again.
  EXPECT_EQ(getSomeVariable2(), 2);
  EXPECT_EQ(getSomeVariable2(), 3);

  // Release the module, and reload it.
  module->reset();
  module = newDynamicModule(testSharedObjectPath("no_op", "c"), false);
  EXPECT_TRUE(module.ok());

  // This module must be the already loaded one, and the variable must be kept.
  const auto getSomeVariable3 =
      module->get()->getFunctionPointer<GetSomeVariableFuncType>("getSomeVariable");
  EXPECT_NE(getSomeVariable3, nullptr);
  EXPECT_EQ(getSomeVariable3(), 4); // Start from 4.
}

/**
 * Class to test the identical behavior of the dynamic module in different languages.
 */
class DynamicModuleTestLanguages : public ::testing::TestWithParam<std::string> {
public:
  static std::string languageParamToTestName(const ::testing::TestParamInfo<std::string>& info) {
    return info.param;
  };
};

INSTANTIATE_TEST_SUITE_P(LanguageTests, DynamicModuleTestLanguages,
                         testing::Values("c", "go"), // TODO: Rust.
                         DynamicModuleTestLanguages::languageParamToTestName);

TEST_P(DynamicModuleTestLanguages, LoadNoOp) {
  std::string language = GetParam();
  absl::StatusOr<DynamicModuleSharedPtr> module =
      newDynamicModule(testSharedObjectPath("no_op", language), false);
  EXPECT_TRUE(module.ok());
}

TEST_P(DynamicModuleTestLanguages, NoProgramInit) {
  std::string language = GetParam();
  absl::StatusOr<DynamicModuleSharedPtr> result =
      newDynamicModule(testSharedObjectPath("no_program_init", language), false);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(), testing::HasSubstr("undefined symbol: envoy_dynamic_module_on_program_init"));
}

TEST_P(DynamicModuleTestLanguages, NoProgramInitFail) {
  std::string language = GetParam();
  absl::StatusOr<DynamicModuleSharedPtr> result =
      newDynamicModule(testSharedObjectPath("no_program_init_fail", language), false);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(result.status().message(), testing::HasSubstr("returned non-zero status: 12345"));
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
