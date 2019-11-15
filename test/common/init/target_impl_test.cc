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

TEST(SharedTargetImplTest, InitializeTwiceBeforeReady) {
  ManagerImpl m1("m1");
  ManagerImpl m2("m2");
  ExpectableWatcherImpl w1;
  ExpectableWatcherImpl w2;
  ExpectableSharedTargetImpl t;
  m1.add(t);
  EXPECT_EQ(0, t.count_);
  m2.add(t);
  EXPECT_EQ(0, t.count_);
  EXPECT_EQ(Manager::State::Uninitialized, m1.state());
  EXPECT_EQ(Manager::State::Uninitialized, m2.state());
  m1.initialize(w1);
  m2.initialize(w2);
  EXPECT_EQ(1, t.count_);
  EXPECT_EQ(Manager::State::Initializing, m1.state());
  EXPECT_EQ(Manager::State::Initializing, m2.state());
  w1.expectReady().Times(1);
  w2.expectReady().Times(1);
  t.ready();
  EXPECT_EQ(Manager::State::Initialized, m1.state());
  EXPECT_EQ(Manager::State::Initialized, m2.state());
}

// Two managers initialize the same target at their own interests.
TEST(SharedTargetImplTest, ConcurrentManagerInitialization) {
  ManagerImpl m1("m1");
  ManagerImpl m2("m2");
  ExpectableWatcherImpl w1;
  ExpectableWatcherImpl w2;
  ExpectableSharedTargetImpl t;
  m1.add(t);
  EXPECT_EQ(0, t.count_);
  m2.add(t);
  EXPECT_EQ(0, t.count_);
  EXPECT_EQ(Manager::State::Uninitialized, m1.state());
  EXPECT_EQ(Manager::State::Uninitialized, m2.state());
  m1.initialize(w1);
  EXPECT_EQ(1, t.count_);
  EXPECT_EQ(Manager::State::Initializing, m1.state());
  EXPECT_EQ(Manager::State::Uninitialized, m2.state());
  w1.expectReady().Times(1);
  t.ready();
  EXPECT_EQ(Manager::State::Initialized, m1.state());
  EXPECT_EQ(Manager::State::Uninitialized, m2.state());
  w1.expectReady().Times(0);
  w2.expectReady().Times(1);
  m2.initialize(w2);
  EXPECT_EQ(Manager::State::Initialized, m2.state());
  EXPECT_EQ(1, t.count_) << "target init function should be invoked only once";
}

// One manager initialized the target, followed by another manager adding the target.
TEST(SharedTargetImplTest, OnLateManagers) {
  ManagerImpl m1("m1");
  ManagerImpl m2("m2");
  ExpectableWatcherImpl w1;
  ExpectableWatcherImpl w2;
  ExpectableSharedTargetImpl t;
  m1.add(t);
  EXPECT_EQ(0, t.count_);
  EXPECT_EQ(Manager::State::Uninitialized, m1.state());
  w1.expectReady().Times(1);
  m1.initialize(w1);
  EXPECT_EQ(1, t.count_);
  EXPECT_EQ(Manager::State::Initializing, m1.state());
  t.ready();
  EXPECT_EQ(Manager::State::Initialized, m1.state());
  EXPECT_EQ(Manager::State::Uninitialized, m2.state());
  w1.expectReady().Times(0);
  w2.expectReady().Times(1);
  m2.initialize(w2);
  EXPECT_EQ(Manager::State::Initialized, m2.state());
}

// If the shared target is destroyed, all the linked init manager will be notified.
TEST(SharedTargetImplTest, DetroyedSharedTargetIsConsideredReadyTarget) {
  ManagerImpl m("test");
  ExpectableWatcherImpl w;
  // Adding shared targets in all kinds of states and destroy the target.
  {
    ExpectableSharedTargetImpl t1("t1");
    m.add(t1);
  }

  {
    ManagerImpl m2("");
    ExpectableSharedTargetImpl t2("t2");
    m2.add(t2);
    m2.initialize(ExpectableWatcherImpl());
    m.add(t2);
  }

  {
    ManagerImpl m3("");
    ExpectableSharedTargetImpl t3("t3");
    m3.add(t3);
    ExpectableWatcherImpl w3;
    w3.expectReady().Times(1);
    m3.initialize(w3);
    m.add(t3);
    t3.ready();
  }

  {
    ManagerImpl m4("");
    ExpectableSharedTargetImpl t4("t4");
    m4.add(t4);
    ExpectableWatcherImpl w4;
    w4.expectReady().Times(1);
    m4.initialize(w4);
    t4.ready();
    m.add(t4);
  }
  // initialization should complete despite the destroyed target
  w.expectReady().Times(1);
  m.initialize(w);
  EXPECT_EQ(Manager::State::Initialized, m.state());
}
} // namespace
} // namespace Init
} // namespace Envoy
