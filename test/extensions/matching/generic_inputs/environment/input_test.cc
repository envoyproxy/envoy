#include "extensions/matching/generic_inputs/environment/input.h"

#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace GenericInputs {
namespace Environment {

TEST(InputTest, BasicUsage) {
  {
    Input input("foo");
    EXPECT_FALSE(input.get().has_value());
  }

  TestEnvironment::setEnvVar("foo", "bar", 1);
  Input input("foo");
  EXPECT_EQ("bar", input.get().value());

  TestEnvironment::unsetEnvVar("foo");
}
} // namespace Environment
} // namespace GenericInputs
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
