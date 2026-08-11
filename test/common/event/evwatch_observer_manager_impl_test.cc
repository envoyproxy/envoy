#include "source/common/event/evwatch_observer_manager_impl.h"
#include "source/common/event/libevent_scheduler.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InvokeWithoutArgs;
using testing::StrictMock;

namespace Envoy {
namespace Event {
namespace {

class MockEvwatchObserver : public Evwatch::Observer {
public:
  MOCK_METHOD(void, onPrepare,
              (MonotonicTime prepare_time, std::optional<MonotonicTime::duration> timeout));
  MOCK_METHOD(void, onCheck, (MonotonicTime check_time));
  MOCK_METHOD(void, onClose, ());
};

class EvwatchObserverManagerImplTest : public testing::Test {
protected:
  EvwatchObserverManagerImplTest()
      : scheduler_(time_source_), manager_(scheduler_.base(), time_source_) {}

  void cycleLoop() {
    auto cb = scheduler_.createSchedulableCallback([]() {});
    cb->scheduleCallbackCurrentIteration();
    scheduler_.run(Dispatcher::RunType::NonBlock);
  }

  Event::TestRealTimeSystem time_source_;
  // LibeventScheduler is used to schedule active events so libevent runs poll iterations and fires
  // evwatch hooks.
  LibeventScheduler scheduler_;
  EvwatchObserverManagerImpl manager_;
};

TEST_F(EvwatchObserverManagerImplTest, DuplicateRegistrationIgnored) {
  StrictMock<MockEvwatchObserver> observer;

  EXPECT_CALL(observer, onPrepare(_, _)).Times(testing::AtLeast(1));
  EXPECT_CALL(observer, onCheck(_)).Times(testing::AtLeast(1));
  EXPECT_CALL(observer, onClose());

  // Registering the same observer twice should be a safe no-op on the second call
  manager_.registerObserver(observer);
  manager_.registerObserver(observer);

  cycleLoop();

  manager_.unregisterObserver(observer);
}

TEST_F(EvwatchObserverManagerImplTest, UnregisterObserverCallsOnClose) {
  StrictMock<MockEvwatchObserver> observer;

  EXPECT_CALL(observer, onClose());

  manager_.registerObserver(observer);
  manager_.unregisterObserver(observer);

  // Subsequent unregister calls on already unregistered observer should be safe no-ops
  manager_.unregisterObserver(observer);
}

TEST_F(EvwatchObserverManagerImplTest, DestructorCallsOnCloseForAllActiveObservers) {
  StrictMock<MockEvwatchObserver> observer1;
  StrictMock<MockEvwatchObserver> observer2;

  EXPECT_CALL(observer1, onClose());
  EXPECT_CALL(observer2, onClose());

  {
    EvwatchObserverManagerImpl manager(scheduler_.base(), time_source_);
    manager.registerObserver(observer1);
    manager.registerObserver(observer2);
  }
}

TEST_F(EvwatchObserverManagerImplTest, UnregisterSelfDuringCallback) {
  StrictMock<MockEvwatchObserver> observer;

  EXPECT_CALL(observer, onPrepare(_, _)).WillOnce(InvokeWithoutArgs([&]() {
    manager_.unregisterObserver(observer);
  }));
  EXPECT_CALL(observer, onClose());
  // onCheck should NOT be called since observer unregistered during onPrepare

  manager_.registerObserver(observer);

  cycleLoop();

  // Subsequent iterations should not invoke observer
  cycleLoop();
}

TEST_F(EvwatchObserverManagerImplTest, UnregisterOtherObserverDuringCallback) {
  StrictMock<MockEvwatchObserver> observer1;
  StrictMock<MockEvwatchObserver> observer2;

  EXPECT_CALL(observer1, onPrepare(_, _)).WillRepeatedly(InvokeWithoutArgs([&]() {
    manager_.unregisterObserver(observer2);
  }));
  EXPECT_CALL(observer1, onCheck(_)).Times(testing::AtLeast(1));
  EXPECT_CALL(observer1, onClose());

  // observer2 should receive onClose when unregistered by observer1 during onPrepare,
  // but MUST NOT receive onPrepare or onCheck.
  EXPECT_CALL(observer2, onClose());

  manager_.registerObserver(observer1);
  manager_.registerObserver(observer2);

  cycleLoop();

  manager_.unregisterObserver(observer1);
}

TEST_F(EvwatchObserverManagerImplTest, DestructorWithObserverUnregisteringSelfInOnClose) {
  StrictMock<MockEvwatchObserver> observer;

  {
    EvwatchObserverManagerImpl manager(scheduler_.base(), time_source_);
    EXPECT_CALL(observer, onClose()).WillOnce(InvokeWithoutArgs([&]() {
      manager.unregisterObserver(observer);
    }));
    manager.registerObserver(observer);
  }
}

} // namespace
} // namespace Event
} // namespace Envoy
