#include "test/mocks/init/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Init {
namespace {

TEST(InitWatcherImplTest, Name) {
  ExpectableWatcherImpl watcher;
  EXPECT_EQ("test", watcher.name());
}

TEST(InitWatcherImplTest, ReadyWhenAvailable) {
  ExpectableWatcherImpl watcher;

  // notifying the watcher through its handle should invoke ready().
  watcher.expectReady();
  EXPECT_TRUE(watcher.createHandle("test")->ready());
}

TEST(InitWatcherImplTest, ReadyWhenUnavailable) {
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
} // namespace Init
} // namespace Envoy
