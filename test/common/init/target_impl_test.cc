#include "test/mocks/init/mocks.h"

#include "gtest/gtest.h"

using ::testing::InSequence;

namespace Envoy {
namespace Init {
namespace {

// Testing common cases for all the target implementation.
template <typename T> class TargetImplTest : public ::testing::Test {};
TYPED_TEST_SUITE_P(TargetImplTest);

template <typename T> std::string getName() { return ""; }
template <> std::string getName<ExpectableTargetImpl>() { return "target test"; }
template <> std::string getName<ExpectableSharedTargetImpl>() { return "shared target test"; }
TYPED_TEST_P(TargetImplTest, Name) {
  TypeParam target;
  EXPECT_EQ(getName<TypeParam>(), target.name());
}

TYPED_TEST_P(TargetImplTest, InitializeWhenAvailable) {
  InSequence s;

  TypeParam target;
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

// Initializing TargetHandle return false if uninitialized SharedTarget is destroyed.
TYPED_TEST_P(TargetImplTest, InitializeWhenUnavailable) {
  InSequence s;
  ExpectableWatcherImpl watcher;
  TargetHandlePtr handle;
  {
    TypeParam target;

    // initializing the target after it's been destroyed should do nothing.
    handle = target.createHandle("test");
    target.expectInitialize().Times(0);
    // target destroyed here
  }
  EXPECT_FALSE(handle->initialize(watcher));
}

TYPED_TEST_P(TargetImplTest, ReadyWhenWatcherUnavailable) {
  TypeParam target;
  {
    ExpectableWatcherImpl watcher;

    // initializing the target through its handle should invoke initialize()...
    target.expectInitialize();
    EXPECT_TRUE(target.createHandle("test")->initialize(watcher));

    // calling ready() on the target after the watcher has been destroyed should do nothing.
    watcher.expectReady().Times(0);
    // watcher destroyed here
  }
  EXPECT_FALSE(target.ready());
}

REGISTER_TYPED_TEST_SUITE_P(TargetImplTest, Name, InitializeWhenAvailable,
                            InitializeWhenUnavailable, ReadyWhenWatcherUnavailable);
using TargetImplTypes = ::testing::Types<ExpectableTargetImpl, ExpectableSharedTargetImpl>;
INSTANTIATE_TYPED_TEST_SUITE_P(Init, TargetImplTest, TargetImplTypes);

TYPED_TEST_SUITE(TargetImplTest, TargetImplTypes);

// Below are the specialized tests for different implementations of Target

// Initializing TargetHandle return false if initialized SharedTarget is destroyed.
TEST(SharedTargetImplTest, ReInitializeWhenUnavailable) {
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
TEST(SharedTargetImplTest, NotifyAllWatcherWhenInitialization) {
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
TEST(SharedTargetImplTest, InitializedSharedTargetNotifyWatcherWhenAddedAgain) {
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

TEST(SharedTargetImplTest, EarlySharedTargetReadyNotifyWatchers) {
  InSequence s;

  ExpectableSharedTargetImpl target;

  // No watcher yet. Nothing will be notified at this moment.
  EXPECT_FALSE(target.ready());

  // It's arguable if the shared target should be initialized after ready()
  // is already invoked.
  target.expectInitialize().Times(0);

  ExpectableWatcherImpl w1;
  TargetHandlePtr handle1 = target.createHandle("m1");
  // w1 is notified with no further target.ready().
  w1.expectReady();
  EXPECT_TRUE(handle1->initialize(w1));

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
