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
    EXPECT_EQ(absl::get<std::string>(input.get()), "foo");
  }

  Input input("foo");
  EXPECT_EQ(absl::get<std::string>(input.get()), "foo");
}
} // namespace EnvironmentVariable
} // namespace CommonInputs
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
