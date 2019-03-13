#include "test/mocks/safe_init/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace SafeInit {
namespace {

// Note that the MockWatcher under scrutiny here is actually a real WatcherImpl, just subclassed for
// use in tests. See test/mocks/safe_init/mocks.h for details.

TEST(WatcherImplTest, Name) {
  MockWatcher watcher;
  EXPECT_EQ("mock watcher", watcher.name());
}

TEST(WatcherImplTest, ReadyWhenAvailable) {
  MockWatcher watcher;

  // notifying the watcher through its handle should invoke ready().
  watcher.expectReady();
  EXPECT_TRUE(watcher.createHandle("test")->ready());
}

TEST(WatcherImplTest, ReadyWhenUnavailable) {
  WatcherHandlePtr handle;
  {
    MockWatcher watcher;

    // notifying the watcher after it's been destroyed should do nothing.
    handle = watcher.createHandle("test");
    watcher.expectReady().Times(0);
  }
  EXPECT_FALSE(handle->ready());
}

} // namespace
} // namespace SafeInit
} // namespace Envoy
