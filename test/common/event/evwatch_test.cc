#include <atomic>
#include <chrono>
#include <memory>
#include <string>

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/evwatch.h"
#include "envoy/event/timer.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::StrictMock;

namespace Envoy {
namespace Event {
namespace {

class MockObserverHandle : public Evwatch::ObserverHandle {
public:
  MOCK_METHOD(Evwatch::ObserverWeakPtr, observer, (), (const, override));
};

class MockEvwatchObserver : public Evwatch::Observer {
public:
  MOCK_METHOD(void, onPrepare,
              (MonotonicTime prepare_time, bool timeout_set, std::chrono::microseconds timeout));
  MOCK_METHOD(void, onCheck, (MonotonicTime check_time));
};

TEST(EvwatchObserverDispatcherTest, MockDispatcherRegistration) {
  auto observer = std::make_unique<StrictMock<MockEvwatchObserver>>();
  MockEvwatchObserver* observer_ptr = observer.get();

  StrictMock<MockDispatcher> dispatcher("worker_0");

  EXPECT_CALL(dispatcher, registerEvwatchObserver(_))
      .WillOnce(Invoke([observer_ptr](Evwatch::ObserverPtr t) {
        EXPECT_EQ(observer_ptr, t.get());
        return std::make_unique<MockObserverHandle>();
      }));

  auto handle = dispatcher.registerEvwatchObserver(std::move(observer));
  EXPECT_NE(nullptr, handle);
}

class EvwatchObserverRealDispatcherTest : public testing::Test {
public:
  EvwatchObserverRealDispatcherTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  void cycleEventLoop() {
    dispatcher_->post([]() {});
    dispatcher_->run(Dispatcher::RunType::Block);
  }

  Api::ApiPtr api_;
  DispatcherPtr dispatcher_;
};

TEST_F(EvwatchObserverRealDispatcherTest, RealLibeventWatcherCallbacksAndRAIIHandle) {
  auto observer = std::make_unique<StrictMock<MockEvwatchObserver>>();
  auto* observer_ptr = observer.get();

  EXPECT_CALL(*observer_ptr, onPrepare(_, _, _)).Times(testing::AtLeast(1));
  EXPECT_CALL(*observer_ptr, onCheck(_)).Times(testing::AtLeast(1));

  auto handle = dispatcher_->registerEvwatchObserver(std::move(observer));
  cycleEventLoop();

  handle.reset();

  // Cycle again; since the handle was reset and we use StrictMock,
  // the test will fail if any callbacks fire after handle destruction.
  cycleEventLoop();
}

TEST_F(EvwatchObserverRealDispatcherTest, NullObserverRegistrationIsNoop) {
  EXPECT_NO_THROW(dispatcher_->registerEvwatchObserver(nullptr));
  cycleEventLoop();
}

TEST_F(EvwatchObserverRealDispatcherTest, MultipleObserversSimultaneous) {
  auto observer1 = std::make_unique<StrictMock<MockEvwatchObserver>>();
  auto observer2 = std::make_unique<StrictMock<MockEvwatchObserver>>();
  auto* observer1_ptr = observer1.get();
  auto* observer2_ptr = observer2.get();

  EXPECT_CALL(*observer1_ptr, onPrepare(_, _, _)).Times(testing::AtLeast(1));
  EXPECT_CALL(*observer1_ptr, onCheck(_)).Times(testing::AtLeast(1));
  EXPECT_CALL(*observer2_ptr, onPrepare(_, _, _)).Times(testing::AtLeast(1));
  EXPECT_CALL(*observer2_ptr, onCheck(_)).Times(testing::AtLeast(1));

  auto handle1 = dispatcher_->registerEvwatchObserver(std::move(observer1));
  auto handle2 = dispatcher_->registerEvwatchObserver(std::move(observer2));

  cycleEventLoop();

  handle1.reset();
  handle2.reset();
}

TEST_F(EvwatchObserverRealDispatcherTest, PartialHandleDestruction) {
  auto observer1 = std::make_unique<StrictMock<MockEvwatchObserver>>();
  auto observer2 = std::make_unique<StrictMock<MockEvwatchObserver>>();
  auto* observer1_ptr = observer1.get();
  auto* observer2_ptr = observer2.get();

  EXPECT_CALL(*observer1_ptr, onPrepare(_, _, _)).Times(testing::AtLeast(1));
  EXPECT_CALL(*observer1_ptr, onCheck(_)).Times(testing::AtLeast(1));
  EXPECT_CALL(*observer2_ptr, onPrepare(_, _, _)).Times(testing::AtLeast(2));
  EXPECT_CALL(*observer2_ptr, onCheck(_)).Times(testing::AtLeast(2));

  auto handle1 = dispatcher_->registerEvwatchObserver(std::move(observer1));
  auto handle2 = dispatcher_->registerEvwatchObserver(std::move(observer2));
  cycleEventLoop();

  // Reset handle1; observer1 should stop receiving callbacks, but observer2 continues.
  handle1.reset();
  cycleEventLoop();

  handle2.reset();
}

TEST_F(EvwatchObserverRealDispatcherTest, TimeoutReportingAndMetricsCalculation) {
  auto observer = std::make_unique<NiceMock<MockEvwatchObserver>>();
  auto* observer_ptr = observer.get();

  MonotonicTime last_check_time{};
  std::chrono::microseconds recorded_loop_duration{0};
  bool observed_timer_timeout{false};

  ON_CALL(*observer_ptr, onCheck(_))
      .WillByDefault(
          Invoke([&last_check_time](MonotonicTime check_time) { last_check_time = check_time; }));

  ON_CALL(*observer_ptr, onPrepare(_, _, _))
      .WillByDefault(Invoke([&](MonotonicTime prepare_time, bool timeout_set,
                                std::chrono::microseconds timeout) {
        if (last_check_time.time_since_epoch().count() != 0 && prepare_time >= last_check_time) {
          recorded_loop_duration =
              std::chrono::duration_cast<std::chrono::microseconds>(prepare_time - last_check_time);
        }
        if (timeout_set && timeout <= std::chrono::microseconds(50000)) {
          observed_timer_timeout = true;
        }
      }));

  auto handle = dispatcher_->registerEvwatchObserver(std::move(observer));

  // Schedule a timer with a 10ms deadline to ensure libevent sets a poll timeout.
  TimerPtr timer = dispatcher_->createTimer([this]() { dispatcher_->exit(); });
  timer->enableTimer(std::chrono::milliseconds(10));

  dispatcher_->run(Dispatcher::RunType::Block);

  // Cycle again to verify duration calculation across iterations.
  cycleEventLoop();

  EXPECT_TRUE(observed_timer_timeout);
  EXPECT_GT(recorded_loop_duration.count(), 0);

  handle.reset();
}

} // namespace
} // namespace Event
} // namespace Envoy
