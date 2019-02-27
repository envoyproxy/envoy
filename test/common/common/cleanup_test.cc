#include "common/common/cleanup.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {
TEST(CleanupTest, ScopeExitCallback) {
  bool callback_fired = false;
  {
    Cleanup cleanup([&callback_fired] { callback_fired = true; });
    EXPECT_FALSE(callback_fired);
  }
  EXPECT_TRUE(callback_fired);
}
} // namespace
} // namespace Envoy
