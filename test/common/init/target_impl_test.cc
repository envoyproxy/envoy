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

TEST(SharedTargetImplTest, OnSyncManagers) {
  ManagerImpl m1("m1");
  ManagerImpl m2("m2");
  int counter = 0;
  SharedTargetImpl t("test", [&counter]() mutable { counter++; });
  m1.add(t);
  EXPECT_EQ(0, counter);
  m2.add(t);
  EXPECT_EQ(0, counter);
  EXPECT_EQ(Manager::State::Uninitialized, m1.state());
  EXPECT_EQ(Manager::State::Uninitialized, m2.state());
  m1.initialize(WatcherImpl("watcher", []() {}));
  m2.initialize(WatcherImpl("watcher", []() {}));
  EXPECT_LE(1, counter);
  EXPECT_EQ(Manager::State::Initializing, m1.state());
  EXPECT_EQ(Manager::State::Initializing, m2.state());

  t.ready();
  EXPECT_EQ(Manager::State::Initialized, m1.state());
  EXPECT_EQ(Manager::State::Initialized, m1.state());
}

TEST(SharedTargetImplTest, OnAsyncManagers) {
  ManagerImpl m1("m1");
  ManagerImpl m2("m2");
  int counter = 0;
  SharedTargetImpl t("test", [&counter]() mutable { counter++; });
  m1.add(t);
  EXPECT_EQ(0, counter);
  m2.add(t);
  EXPECT_EQ(0, counter);
  EXPECT_EQ(Manager::State::Uninitialized, m1.state());
  EXPECT_EQ(Manager::State::Uninitialized, m2.state());
  m1.initialize(WatcherImpl("watcher", []() {}));
  EXPECT_EQ(1, counter);
  EXPECT_EQ(Manager::State::Initializing, m1.state());
  EXPECT_EQ(Manager::State::Uninitialized, m2.state());
  t.ready();
  EXPECT_EQ(Manager::State::Initialized, m1.state());
  EXPECT_EQ(Manager::State::Uninitialized, m2.state());
  m2.initialize(WatcherImpl("watcher", []() {}));
  EXPECT_EQ(Manager::State::Initialized, m2.state());
}

TEST(SharedTargetImplTest, OnLateManagers) {
  ManagerImpl m1("m1");
  ManagerImpl m2("m2");
  int counter = 0;
  SharedTargetImpl t("test", [&counter]() mutable { counter++; });
  m1.add(t);
  EXPECT_EQ(0, counter);
  EXPECT_EQ(Manager::State::Uninitialized, m1.state());
  m1.initialize(WatcherImpl("watcher", []() {}));
  EXPECT_EQ(1, counter);
  EXPECT_EQ(Manager::State::Initializing, m1.state());
  t.ready();
  EXPECT_EQ(Manager::State::Initialized, m1.state());

  EXPECT_EQ(Manager::State::Uninitialized, m2.state());
  m2.initialize(WatcherImpl("watcher", []() {}));
  EXPECT_EQ(Manager::State::Initialized, m2.state());
}

TEST(SharedTargetImplTest, GoneTargetIsInitialized) {

  ManagerImpl m("test");
  int counter = 0;

  // add a target and destroy it
  {
    SharedTargetImpl t("t1", [&counter]() mutable { counter++; });
    m.add(t);
  }

  {
    ManagerImpl m2("");
    SharedTargetImpl t2("t2", []() mutable {});
    m2.add(t2);
    m2.initialize(WatcherImpl("watcher", []() {}));
    m.add(t2);
  }
  {
    ManagerImpl m3("");
    SharedTargetImpl t3("t3", []() mutable {});
    m3.add(t3);
    m3.initialize(WatcherImpl("watcher", []() {}));
    m.add(t3);
    t3.ready();
  }

  {
    ManagerImpl m4("");
    SharedTargetImpl t4("t4", []() mutable {});
    m4.add(t4);
    m4.initialize(WatcherImpl("watcher", []() {}));
    t4.ready();
    m.add(t4);
  }
  // initialization should complete despite the destroyed target
  m.initialize(WatcherImpl("watcher", []() {}));
  EXPECT_EQ(Manager::State::Initialized, m.state());
}
} // namespace
} // namespace Init
} // namespace Envoy
