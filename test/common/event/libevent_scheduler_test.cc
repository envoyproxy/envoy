#include "source/common/event/libevent_scheduler.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/test_time.h"
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
  LibeventSchedulerTest() : scheduler_(time_system_) {}

  void cycleScheduler() {
    auto cb = scheduler_.createSchedulableCallback([]() {});
    cb->scheduleCallbackCurrentIteration();
    scheduler_.run(Dispatcher::RunType::NonBlock);
  }

  Event::TestRealTimeSystem time_system_;
  LibeventScheduler scheduler_;
};

class MockEvwatchObserver : public Evwatch::Observer {
public:
  MOCK_METHOD(void, onPrepare,
              (MonotonicTime prepare_time, std::optional<MonotonicTime::duration> timeout));
  MOCK_METHOD(void, onCheck, (MonotonicTime check_time));
  MOCK_METHOD(void, onClose, ());
};

TEST_F(LibeventSchedulerTest, RegisterAndUnregisterObservers) {
  StrictMock<MockEvwatchObserver> observer1;
  StrictMock<MockEvwatchObserver> observer2;

  EXPECT_CALL(observer1, onPrepare(_, _)).Times(testing::AtLeast(1));
  EXPECT_CALL(observer1, onCheck(_)).Times(testing::AtLeast(1));
  EXPECT_CALL(observer1, onClose());
  EXPECT_CALL(observer2, onPrepare(_, _)).Times(testing::AtLeast(1));
  EXPECT_CALL(observer2, onCheck(_)).Times(testing::AtLeast(1));
  EXPECT_CALL(observer2, onClose());

  scheduler_.registerEvwatchObserver(observer1);
  scheduler_.registerEvwatchObserver(observer2);

  cycleScheduler();

  // Unregister observer1; observer2 continues
  scheduler_.unregisterEvwatchObserver(observer1);
  cycleScheduler();

  // Unregister observer2
  scheduler_.unregisterEvwatchObserver(observer2);
  cycleScheduler();

  // Cycle again when evwatch_observers_ is empty
  cycleScheduler();
}

class MockTimeSource : public TimeSource {
public:
  MOCK_METHOD(SystemTime, systemTime, (), (override));
  MOCK_METHOD(MonotonicTime, monotonicTime, (), (override));
};

TEST_F(LibeventSchedulerTest, OnCloseCalledDuringSchedulerDestruction) {
  StrictMock<MockTimeSource> time_source;
  StrictMock<MockEvwatchObserver> observer;

  EXPECT_CALL(observer, onClose());

  {
    LibeventScheduler scheduler(time_source);
    scheduler.registerEvwatchObserver(observer);
  }
}

TEST_F(LibeventSchedulerTest, CustomTimeSourceUsedForObserverCallbacks) {
  StrictMock<MockTimeSource> time_source;
  LibeventScheduler scheduler(time_source);

  StrictMock<MockEvwatchObserver> observer;

  const MonotonicTime mock_time = MonotonicTime(std::chrono::nanoseconds(123456789));

  EXPECT_CALL(time_source, monotonicTime()).Times(2).WillRepeatedly(testing::Return(mock_time));
  EXPECT_CALL(observer, onPrepare(mock_time, _));
  EXPECT_CALL(observer, onCheck(mock_time));
  EXPECT_CALL(observer, onClose());

  scheduler.registerEvwatchObserver(observer);

  auto cb = scheduler.createSchedulableCallback([]() {});
  cb->scheduleCallbackCurrentIteration();
  scheduler.run(Dispatcher::RunType::NonBlock);

  scheduler.unregisterEvwatchObserver(observer);
}

TEST_F(LibeventSchedulerTest, EvwatchObserverManagerDelegation) {
  StrictMock<MockTimeSource> time_source;
  auto evwatch_manager = std::make_unique<StrictMock<MockEvwatchObserverManager>>();
  auto* mock_manager = evwatch_manager.get();

  StrictMock<MockEvwatchObserver> observer;

  EXPECT_CALL(*mock_manager, registerObserver(testing::Ref(observer)));
  EXPECT_CALL(*mock_manager, unregisterObserver(testing::Ref(observer)));

  LibeventScheduler scheduler(time_source, std::move(evwatch_manager));

  scheduler.registerEvwatchObserver(observer);
  scheduler.unregisterEvwatchObserver(observer);
}

} // namespace
} // namespace Event
} // namespace Envoy
