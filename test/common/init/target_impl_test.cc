#include "common/init/manager_impl.h"

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

// SharedTarget acts as TargetImpl if single watcher is provided.
TEST(InitSharedTargetTestImpl, InitializeSingleWatcher) {
  InSequence s;

  ExpectableSharedTargetImpl target;
  ExpectableWatcherImpl watcher;

  // initializing the target through its handle should invoke initialize()...
  target.expectInitialize();
  EXPECT_TRUE(target.createHandle("test")->initialize(watcher));

  // calling ready() on the target should invoke the saved watcher handle...
  watcher.expectReady();
  EXPECT_TRUE(target.ready());

  // calling ready() a second time should have no effect.
  watcher.expectReady().Times(0);
  target.ready();
}

// Initializing TargetHandle return false if uninitialized SharedTarget is destroyed.
TEST(InitSharedTargetTestImpl, InitializeWhenUnavailable) {
  InSequence s;
  ExpectableWatcherImpl watcher;
  TargetHandlePtr handle;
  {
    ExpectableSharedTargetImpl target;

    // initializing the target after it's been destroyed should do nothing.
    handle = target.createHandle("test");
    target.expectInitialize().Times(0);
  }
  EXPECT_FALSE(handle->initialize(watcher));
}

// Initializing TargetHandle return false if initialized SharedTarget is destroyed.
TEST(InitSharedTargetTestImpl, ReInitializeWhenUnavailable) {
  InSequence s;
  ExpectableWatcherImpl w;
  TargetHandlePtr handle;
  {
    ExpectableSharedTargetImpl target;

    target.expectInitialize();
    TargetHandlePtr handle1 = target.createHandle("m1");
    ExpectableWatcherImpl w1;
    EXPECT_TRUE(handle1->initialize(w1));

    // initializing the target after it's been destroyed should do nothing.
    handle = target.createHandle("m2");
    target.expectInitialize().Times(0);
    // target destroyed
  }
  EXPECT_FALSE(handle->initialize(w));
}

// SharedTarget notifies multiple watchers.
TEST(InitSharedTargetTestImpl, NotifyAllWatcherWhenInitialization) {
  InSequence s;
  ExpectableWatcherImpl w1;
  ExpectableSharedTargetImpl target;

  target.expectInitialize();
  TargetHandlePtr handle1 = target.createHandle("m1");
  EXPECT_TRUE(handle1->initialize(w1));

  ExpectableWatcherImpl w2;
  target.expectInitialize().Times(0);
  TargetHandlePtr handle2 = target.createHandle("m2");
  // calling ready() on the target should invoke all the saved watchers.
  w1.expectReady();
  EXPECT_TRUE(target.ready());
  w2.expectReady();
  EXPECT_TRUE(handle2->initialize(w2));
}

// Initialized SharedTarget notifies further watcher immediately at second initialization attempt.
TEST(InitSharedTargetTestImpl, InitializedSharedTargetNotifyWatcherWhenAddedAgain) {
  InSequence s;
  ExpectableWatcherImpl w1;
  ExpectableSharedTargetImpl target;

  target.expectInitialize();
  TargetHandlePtr handle1 = target.createHandle("m1");
  EXPECT_TRUE(handle1->initialize(w1));

  // calling ready() on the target should invoke the saved watcher handle(s).
  w1.expectReady();
  EXPECT_TRUE(target.ready());

  ExpectableWatcherImpl w2;
  target.expectInitialize().Times(0);
  TargetHandlePtr handle2 = target.createHandle("m2");
  // w2 is notified with no further target.ready().
  w2.expectReady();
  EXPECT_TRUE(handle2->initialize(w2));
}

} // namespace
} // namespace Init
} // namespace Envoy
