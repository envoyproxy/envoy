#include "extensions/matching/generic_inputs/environment_variable/input.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace GenericInputs {
namespace EnvironmentVariable {

TEST(InputTest, BasicUsage) {
  {
    Input input("foo");
    ASSERT_TRUE(input.get().has_value());
    EXPECT_EQ(input.get().value(), "foo");
  }

  Input input("foo");
  ASSERT_TRUE(input.get().has_value());
  EXPECT_EQ(input.get().value(), "foo");
}
} // namespace Environment
} // namespace GenericInputs
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
