#include "source/common/common/containers.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Common {

TEST(ApplyToAllWithCompletionCallbackTest, BasicUsage) {
  {
    std::vector<int> container{1, 2, 3};
    std::vector<int> cb_invoked_with;
    bool done_cb_called = false;

    applyToAllWithCleanup<int>(
        container,
        [&cb_invoked_with, &done_cb_called](int i, std::shared_ptr<Cleanup>) {
          cb_invoked_with.emplace_back(i);
          EXPECT_FALSE(done_cb_called);
        },
        [&done_cb_called]() { done_cb_called = true; });

    EXPECT_TRUE(done_cb_called);
  }

  std::vector<int> container{1, 2, 3};
  std::vector<int> cb_invoked_with;
  bool done_cb_called = false;

  std::shared_ptr<Cleanup> delayed_cleanup;

  applyToAllWithCleanup<int>(
      container,
      [&cb_invoked_with, &done_cb_called, &delayed_cleanup](int i,
                                                            std::shared_ptr<Cleanup> cleanup) {
        cb_invoked_with.emplace_back(i);
        EXPECT_FALSE(done_cb_called);
        delayed_cleanup = cleanup;
      },
      [&done_cb_called]() { done_cb_called = true; });

  EXPECT_FALSE(done_cb_called);
  delayed_cleanup.reset();
  EXPECT_TRUE(done_cb_called);
}
} // namespace Common
} // namespace Envoy
