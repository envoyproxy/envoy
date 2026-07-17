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
};

TEST_F(LibeventSchedulerTest, RegisterAndUnregisterObservers) {
  auto observer1 = std::make_unique<StrictMock<MockEvwatchObserver>>();
  auto observer2 = std::make_unique<StrictMock<MockEvwatchObserver>>();
  auto* observer1_ptr = observer1.get();
  auto* observer2_ptr = observer2.get();

  EXPECT_CALL(*observer1_ptr, onPrepare(_, _)).Times(testing::AtLeast(1));
  EXPECT_CALL(*observer1_ptr, onCheck(_)).Times(testing::AtLeast(1));
  EXPECT_CALL(*observer2_ptr, onPrepare(_, _)).Times(testing::AtLeast(1));
  EXPECT_CALL(*observer2_ptr, onCheck(_)).Times(testing::AtLeast(1));

  scheduler_.registerEvwatchObserver(std::move(observer1));
  scheduler_.registerEvwatchObserver(std::move(observer2));

  cycleScheduler();

  // Unregister observer1; observer2 continues
  scheduler_.unregisterEvwatchObserver(observer1_ptr);
  cycleScheduler();

  // Unregister observer2
  scheduler_.unregisterEvwatchObserver(observer2_ptr);
  cycleScheduler();

  // Cycle again when evwatch_observers_ is empty
  cycleScheduler();
}

TEST_F(LibeventSchedulerTest, RegisterNullObserver) {
  scheduler_.registerEvwatchObserver(nullptr);
  scheduler_.unregisterEvwatchObserver(nullptr);
  cycleScheduler();
}

class MockTimeSource : public TimeSource {
public:
  MOCK_METHOD(SystemTime, systemTime, (), (override));
  MOCK_METHOD(MonotonicTime, monotonicTime, (), (override));
};

TEST_F(LibeventSchedulerTest, CustomTimeSourceUsedForObserverCallbacks) {
  StrictMock<MockTimeSource> time_source;
  LibeventScheduler scheduler(time_source);

  auto observer = std::make_unique<StrictMock<MockEvwatchObserver>>();
  auto* observer_ptr = observer.get();

  const MonotonicTime mock_time = MonotonicTime(std::chrono::nanoseconds(123456789));

  EXPECT_CALL(time_source, monotonicTime()).Times(2).WillRepeatedly(testing::Return(mock_time));
  EXPECT_CALL(*observer_ptr, onPrepare(mock_time, _));
  EXPECT_CALL(*observer_ptr, onCheck(mock_time));

  scheduler.registerEvwatchObserver(std::move(observer));

  auto cb = scheduler.createSchedulableCallback([]() {});
  cb->scheduleCallbackCurrentIteration();
  scheduler.run(Dispatcher::RunType::NonBlock);

  scheduler.unregisterEvwatchObserver(observer_ptr);
}

} // namespace
} // namespace Event
} // namespace Envoy
