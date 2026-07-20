#include "source/common/event/evwatch_observer_manager_impl.h"
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

class MockEvwatchObserver : public Evwatch::Observer {
public:
  MOCK_METHOD(void, onPrepare,
              (MonotonicTime prepare_time, std::optional<MonotonicTime::duration> timeout));
  MOCK_METHOD(void, onCheck, (MonotonicTime check_time));
  MOCK_METHOD(void, onClose, ());
};

class EvwatchObserverManagerImplTest : public testing::Test {
protected:
  EvwatchObserverManagerImplTest() : scheduler_(time_source_) {}

  Event::TestRealTimeSystem time_source_;
  LibeventScheduler scheduler_;
};

TEST_F(EvwatchObserverManagerImplTest, DuplicateRegistrationIgnored) {
  StrictMock<MockEvwatchObserver> observer;

  EXPECT_CALL(observer, onPrepare(_, _)).Times(testing::AtLeast(1));
  EXPECT_CALL(observer, onCheck(_)).Times(testing::AtLeast(1));
  EXPECT_CALL(observer, onClose());

  // Registering the same observer twice should be a safe no-op on the second call
  scheduler_.registerEvwatchObserver(observer);
  scheduler_.registerEvwatchObserver(observer);

  auto cb = scheduler_.createSchedulableCallback([]() {});
  cb->scheduleCallbackCurrentIteration();
  scheduler_.run(Dispatcher::RunType::NonBlock);

  scheduler_.unregisterEvwatchObserver(observer);
}

TEST_F(EvwatchObserverManagerImplTest, UnregisterObserverCallsOnClose) {
  StrictMock<MockEvwatchObserver> observer;

  EXPECT_CALL(observer, onClose());

  scheduler_.registerEvwatchObserver(observer);
  scheduler_.unregisterEvwatchObserver(observer);

  // Subsequent unregister calls on already unregistered observer should be safe no-ops
  scheduler_.unregisterEvwatchObserver(observer);
}

TEST_F(EvwatchObserverManagerImplTest, DestructorCallsOnCloseForAllActiveObservers) {
  StrictMock<MockEvwatchObserver> observer1;
  StrictMock<MockEvwatchObserver> observer2;

  EXPECT_CALL(observer1, onClose());
  EXPECT_CALL(observer2, onClose());

  {
    LibeventScheduler scheduler(time_source_);
    scheduler.registerEvwatchObserver(observer1);
    scheduler.registerEvwatchObserver(observer2);
  }
}

} // namespace
} // namespace Event
} // namespace Envoy
