#include "source/extensions/matching/common_inputs/environment_variable/input.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace CommonInputs {
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
} // namespace EnvironmentVariable
} // namespace CommonInputs
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
