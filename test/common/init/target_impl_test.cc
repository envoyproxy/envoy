#include "test/mocks/init/mocks.h"

#include "gtest/gtest.h"

using ::testing::InSequence;

namespace Envoy {
namespace Init {
namespace {

TEST(InitTargetImplTest, Name) {
  ExpectableTargetImpl target;
  EXPECT_EQ("target test", target.name());
}

TEST(InitTargetImplTest, InitializeWhenAvailable) {
  InSequence s;

  ExpectableTargetImpl target;
  ExpectableWatcherImpl watcher;

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

TEST(InitTargetImplTest, InitializeWhenUnavailable) {
  ExpectableWatcherImpl watcher;
  TargetHandlePtr handle;
  {
    ExpectableTargetImpl target;

    // initializing the target after it's been destroyed should do nothing.
    handle = target.createHandle("test");
    target.expectInitialize().Times(0);
  }
  EXPECT_FALSE(handle->initialize(watcher));
}

TEST(InitTargetImplTest, ReadyWhenWatcherUnavailable) {
  ExpectableTargetImpl target;
  {
    ExpectableWatcherImpl watcher;

    // initializing the target through its handle should invoke initialize()...
    target.expectInitialize();
    EXPECT_TRUE(target.createHandle("test")->initialize(watcher));

    // calling ready() on the target after the watcher has been destroyed should do nothing.
    watcher.expectReady().Times(0);
  }
  EXPECT_FALSE(target.ready());
}

} // namespace
} // namespace Init
} // namespace Envoy
