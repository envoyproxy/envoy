#include "common/common/containers.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Common {

TEST(ApplyToAllWithCompletionCallbackTest, BasicUsage) {
  std::vector<int> container{1, 2, 3};
  std::vector<int> cb_invoked_with;
  bool done_cb_called = false;

  applyToAllWithCompletionCallback(
      container,
      [&cb_invoked_with, &done_cb_called](int i, std::function<void()> doneF) {
        cb_invoked_with.emplace_back(i);
        EXPECT_FALSE(done_cb_called);
        doneF();
      },
      [&done_cb_called]() { done_cb_called = true; });

  EXPECT_TRUE(done_cb_called);
}
} // namespace Common
} // namespace Envoy