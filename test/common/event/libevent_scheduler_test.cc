#include "source/common/event/libevent_scheduler.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::StrictMock;

namespace Envoy {
namespace Event {
namespace {

class LibeventSchedulerTest : public testing::Test {
protected:
  LibeventSchedulerTest() = default;

  void cycleScheduler() {
    auto cb = scheduler_.createSchedulableCallback([]() {});
    cb->scheduleCallbackCurrentIteration();
    scheduler_.run(Dispatcher::RunType::NonBlock);
  }

  LibeventScheduler scheduler_;
};

class MockEvwatchObserver : public Evwatch::Observer {
public:
  MOCK_METHOD(void, onPrepare, (uint64_t prepare_time_us, bool timeout_set, uint64_t timeout_us));
  MOCK_METHOD(void, onCheck, (uint64_t check_time_us));
};

TEST_F(LibeventSchedulerTest, RegisterMultipleObserversAndLazyPruning) {
  auto observer1 = std::make_shared<StrictMock<MockEvwatchObserver>>();
  auto observer2 = std::make_shared<StrictMock<MockEvwatchObserver>>();
  auto* observer1_ptr = observer1.get();
  auto* observer2_ptr = observer2.get();

  EXPECT_CALL(*observer1_ptr, onPrepare(_, _, _)).Times(testing::AtLeast(1));
  EXPECT_CALL(*observer1_ptr, onCheck(_)).Times(testing::AtLeast(1));
  EXPECT_CALL(*observer2_ptr, onPrepare(_, _, _)).Times(testing::AtLeast(1));
  EXPECT_CALL(*observer2_ptr, onCheck(_)).Times(testing::AtLeast(1));

  // First observer registers libevent watchers (evwatch_observers_registered_ == false)
  auto handle1 = scheduler_.registerEvwatchObserver(std::move(observer1));
  EXPECT_NE(nullptr, handle1);

  // Second observer appends to vector without re-registering libevent watchers
  // (evwatch_observers_registered_ == true)
  auto handle2 = scheduler_.registerEvwatchObserver(std::move(observer2));
  EXPECT_NE(nullptr, handle2);

  cycleScheduler();

  // Reset handle1; observer1 will hit the erase branch in both onPrepare and onCheck, while
  // observer2 continues
  handle1.reset();
  cycleScheduler();

  // Reset handle2; observer2 is erased, leaving evwatch_observers_ empty
  handle2.reset();
  cycleScheduler();

  // Cycle again when evwatch_observers_ is already empty at the start of onPrepare and onCheck
  cycleScheduler();
}

TEST_F(LibeventSchedulerTest, RegisterNullObserver) {
  auto handle = scheduler_.registerEvwatchObserver(nullptr);
  EXPECT_EQ(nullptr, handle);
  cycleScheduler();
}

TEST_F(LibeventSchedulerTest, DestructionDuringPrepareCallback) {
  auto observer1 = std::make_shared<StrictMock<MockEvwatchObserver>>();
  auto observer2 = std::make_shared<StrictMock<MockEvwatchObserver>>();
  auto* observer1_ptr = observer1.get();
  auto* observer2_ptr = observer2.get();

  Evwatch::ObserverHandlePtr handle1, handle2;

  EXPECT_CALL(*observer1_ptr, onPrepare(_, _, _))
      .Times(testing::AtLeast(1))
      .WillRepeatedly(testing::InvokeWithoutArgs([&]() {
        // Destroy handle2 during onPrepare, causing observer2's lock() to fail during onCheck in
        // the same iteration!
        handle2.reset();
      }));
  EXPECT_CALL(*observer1_ptr, onCheck(_)).Times(testing::AtLeast(1));

  // observer2 should receive onPrepare, but NOT onCheck since it gets reset during observer1's
  // onPrepare
  EXPECT_CALL(*observer2_ptr, onPrepare(_, _, _)).Times(testing::AtLeast(1));

  // Register observer2 first so it receives onPrepare before observer1's onPrepare resets handle2!
  handle2 = scheduler_.registerEvwatchObserver(std::move(observer2));
  handle1 = scheduler_.registerEvwatchObserver(std::move(observer1));

  cycleScheduler();

  handle1.reset();
  cycleScheduler();
}

} // namespace
} // namespace Event
} // namespace Envoy
