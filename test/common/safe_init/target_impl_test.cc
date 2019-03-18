#include "test/mocks/safe_init/mocks.h"

#include "gtest/gtest.h"

using ::testing::InSequence;

namespace Envoy {
namespace SafeInit {
namespace {

// Note that the "mock" objects under scrutiny here are actually the real TargetImpl and
// WatcherImpl, just subclassed for use in tests. See test/mocks/safe_init/mocks.h for details.

TEST(SafeInitTargetImplTest, Name) {
  MockTarget target;
  EXPECT_EQ("target mock", target.name());
}

TEST(SafeInitTargetImplTest, InitializeWhenAvailable) {
  InSequence s;

  MockTarget target;
  MockWatcher watcher;

  // initializing the target through its handle should invoke initialize()...
  target.expectInitialize();
  EXPECT_TRUE(target.createHandle("test")->initialize(watcher));

  // calling ready() on the target should invoke the saved watcher handle...
  watcher.expectReady();
  EXPECT_TRUE(target.ready());

  // calling ready() a second time should have no effect.
  watcher.expectReady().Times(0);
  EXPECT_FALSE(target.ready());
}

TEST(SafeInitTargetImplTest, InitializeWhenUnavailable) {
  MockWatcher watcher;
  TargetHandlePtr handle;
  {
    MockTarget target;

    // initializing the target after it's been destroyed should do nothing.
    handle = target.createHandle("test");
    target.expectInitialize().Times(0);
  }
  EXPECT_FALSE(handle->initialize(watcher));
}

TEST(SafeInitTargetImplTest, ReadyWhenWatcherUnavailable) {
  MockTarget target;
  {
    MockWatcher watcher;

    // initializing the target through its handle should invoke initialize()...
    target.expectInitialize();
    EXPECT_TRUE(target.createHandle("test")->initialize(watcher));

    // calling ready() on the target after the watcher has been destroyed should do nothing.
    watcher.expectReady().Times(0);
  }
  EXPECT_FALSE(target.ready());
}

} // namespace
} // namespace SafeInit
} // namespace Envoy
