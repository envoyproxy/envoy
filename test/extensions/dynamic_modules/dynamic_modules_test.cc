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
std::string testSharedObjectPath(std::string name) {
  return TestEnvironment::substitute(
             "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/") +
         "lib" + name + ".so";
}

TEST(DynamicModuleTest, InvalidPathException) {
  EXPECT_THROW_WITH_REGEX(DynamicModule("invalid_name", false), EnvoyException,
                          "Failed to load dynamic module: invalid_name : ");
}

TEST(DynamicModuleTest, LoadNoOp) {
  using GetSomeVariableFuncType = int (*)();
  DynamicModulesSharedPtr module =
      std::make_shared<DynamicModule>(testSharedObjectPath("no_op"), false);
  const auto getSomeVariable = module->getTypedSymbol<GetSomeVariableFuncType>("getSomeVariable");
  EXPECT_EQ(getSomeVariable(), 1);
  EXPECT_EQ(getSomeVariable(), 2);
  EXPECT_EQ(getSomeVariable(), 3);

  // Release the module, and reload it.
  module.reset();
  module = std::make_shared<DynamicModule>(testSharedObjectPath("no_op"),
                                           true); // This time, do not close the module.

  // This module must be reloaded and the variable must be reset.
  const auto getSomeVariable2 =
      (module->getTypedSymbol<GetSomeVariableFuncType>("getSomeVariable"));
  EXPECT_NE(getSomeVariable2, nullptr);
  EXPECT_EQ(getSomeVariable2(), 1); // Start from 1 again.
  EXPECT_EQ(getSomeVariable2(), 2);
  EXPECT_EQ(getSomeVariable2(), 3);

  // Release the module, and reload it.
  module.reset();
  module = std::make_shared<DynamicModule>(testSharedObjectPath("no_op"), false);

  // This module must be the already loaded one, and the variable must be kept.
  const auto getSomeVariable3 = module->getTypedSymbol<GetSomeVariableFuncType>("getSomeVariable");
  EXPECT_NE(getSomeVariable3, nullptr);
  EXPECT_EQ(getSomeVariable3(), 4); // Start from 4.
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
