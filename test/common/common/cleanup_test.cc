#include "common/common/cleanup.h"

#include "test/test_common/test_base.h"

namespace Envoy {

using CleanupTest = TestBase;

TEST_F(CleanupTest, ScopeExitCallback) {
  bool callback_fired = false;
  {
    Cleanup cleanup([&callback_fired] { callback_fired = true; });
    EXPECT_FALSE(callback_fired);
  }
  EXPECT_TRUE(callback_fired);
}

} // namespace Envoy
