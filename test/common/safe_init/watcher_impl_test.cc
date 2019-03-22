#include "test/mocks/safe_init/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace SafeInit {
namespace {

TEST(SafeInitWatcherImplTest, Name) {
  ExpectableWatcherImpl watcher;
  EXPECT_EQ("test", watcher.name());
}

TEST(SafeInitWatcherImplTest, ReadyWhenAvailable) {
  ExpectableWatcherImpl watcher;

  // notifying the watcher through its handle should invoke ready().
  watcher.expectReady();
  EXPECT_TRUE(watcher.createHandle("test")->ready());
}

TEST(SafeInitWatcherImplTest, ReadyWhenUnavailable) {
  WatcherHandlePtr handle;
  {
    ExpectableWatcherImpl watcher;

    // notifying the watcher after it's been destroyed should do nothing.
    handle = watcher.createHandle("test");
    watcher.expectReady().Times(0);
  }
  EXPECT_FALSE(handle->ready());
}

} // namespace
} // namespace SafeInit
} // namespace Envoy
