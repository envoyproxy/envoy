#include "envoy/network/exception.h"

#include "source/common/api/api_impl.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/server/worker_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/guard_dog.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/overload_manager.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Server {
namespace {

std::function<void()> emptyCallback = []() {};

class WorkerImplTest : public testing::Test {
public:
  WorkerImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("worker_test")),
        no_exit_timer_(dispatcher_->createTimer([]() -> void {})),
        stat_names_(api_->rootScope().symbolTable()),
        worker_(tls_, hooks_, std::move(dispatcher_), Network::ConnectionHandlerPtr{handler_},
                overload_manager_, *api_, stat_names_) {
    // In the real worker the watchdog has timers that prevent exit. Here we need to prevent event
    // loop exit since we use mock timers.
    no_exit_timer_->enableTimer(std::chrono::hours(1));
  }

  ~WorkerImplTest() override {
    // We init no_exit_timer_ before worker_ because the dispatcher will be
    // moved into the worker. However we need to destruct no_exit_timer_ before
    // destructing the worker, otherwise the timer will outlive its dispatcher.
    no_exit_timer_.reset();
  }

  NiceMock<Runtime::MockLoader> runtime_;
  testing::NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Network::MockConnectionHandler* handler_ = new Network::MockConnectionHandler();
  NiceMock<MockGuardDog> guard_dog_;
  NiceMock<MockOverloadManager> overload_manager_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  DefaultListenerHooks hooks_;
  Event::TimerPtr no_exit_timer_;
  WorkerStatNames stat_names_;
  WorkerImpl worker_;
};

TEST_F(WorkerImplTest, BasicFlow) {
  InSequence s;
  std::thread::id current_thread_id = std::this_thread::get_id();
  ConditionalInitializer ci;

  // Before a worker is started adding a listener will be posted and will get added when the
  // thread starts running.
  NiceMock<Network::MockListenerConfig> listener;
  ON_CALL(listener, listenerTag()).WillByDefault(Return(1UL));
  EXPECT_CALL(*handler_, addListener(_, _, _, _))
      .WillOnce(
          Invoke([current_thread_id](absl::optional<uint64_t>, Network::ListenerConfig& config,
                                     Runtime::Loader&, Random::RandomGenerator&) -> void {
            EXPECT_EQ(config.listenerTag(), 1UL);
            EXPECT_NE(current_thread_id, std::this_thread::get_id());
          }));
  worker_.addListener(
      absl::nullopt, listener, [&ci]() -> void { ci.setReady(); }, runtime_, random_);

  NiceMock<Stats::MockStore> store;
  worker_.start(guard_dog_, emptyCallback);
  worker_.initializeStats(*store.rootScope());
  ci.waitReady();

  // After a worker is started adding/stopping/removing a listener happens on the worker thread.
  NiceMock<Network::MockListenerConfig> listener2;
  ON_CALL(listener2, listenerTag()).WillByDefault(Return(2UL));
  EXPECT_CALL(*handler_, addListener(_, _, _, _))
      .WillOnce(
          Invoke([current_thread_id](absl::optional<uint64_t>, Network::ListenerConfig& config,
                                     Runtime::Loader&, Random::RandomGenerator&) -> void {
            EXPECT_EQ(config.listenerTag(), 2UL);
            EXPECT_NE(current_thread_id, std::this_thread::get_id());
          }));
  worker_.addListener(
      absl::nullopt, listener2, [&ci]() -> void { ci.setReady(); }, runtime_, random_);
  ci.waitReady();

  EXPECT_CALL(*handler_, stopListeners(2, _))
      .WillOnce(InvokeWithoutArgs([current_thread_id, &ci]() -> void {
        EXPECT_NE(current_thread_id, std::this_thread::get_id());
        ci.setReady();
      }));

  ConditionalInitializer ci2;
  // Verify that callback is called from the other thread.
  worker_.stopListener(listener2, {}, [current_thread_id, &ci2]() {
    EXPECT_NE(current_thread_id, std::this_thread::get_id());
    ci2.setReady();
  });
  ci.waitReady();
  ci2.waitReady();

  EXPECT_CALL(*handler_, removeListeners(2))
      .WillOnce(InvokeWithoutArgs([current_thread_id]() -> void {
        EXPECT_NE(current_thread_id, std::this_thread::get_id());
      }));
  worker_.removeListener(listener2, [current_thread_id, &ci]() -> void {
    EXPECT_NE(current_thread_id, std::this_thread::get_id());
    ci.setReady();
  });
  ci.waitReady();

  // Now test adding and removing a listener without stopping it first.
  NiceMock<Network::MockListenerConfig> listener3;
  ON_CALL(listener3, listenerTag()).WillByDefault(Return(3UL));
  EXPECT_CALL(*handler_, addListener(_, _, _, _))
      .WillOnce(
          Invoke([current_thread_id](absl::optional<uint64_t>, Network::ListenerConfig& config,
                                     Runtime::Loader&, Random::RandomGenerator&) -> void {
            EXPECT_EQ(config.listenerTag(), 3UL);
            EXPECT_NE(current_thread_id, std::this_thread::get_id());
          }));
  worker_.addListener(
      absl::nullopt, listener3, [&ci]() -> void { ci.setReady(); }, runtime_, random_);
  ci.waitReady();

  EXPECT_CALL(*handler_, removeListeners(3))
      .WillOnce(InvokeWithoutArgs([current_thread_id]() -> void {
        EXPECT_NE(current_thread_id, std::this_thread::get_id());
      }));
  worker_.removeListener(listener3, [current_thread_id]() -> void {
    EXPECT_NE(current_thread_id, std::this_thread::get_id());
  });

  worker_.stop();
}

TEST_F(WorkerImplTest, WorkerInvokesProvidedCallback) {
  absl::Notification callback_ran;
  auto cb = [&callback_ran]() { callback_ran.Notify(); };
  worker_.start(guard_dog_, cb);

  callback_ran.WaitForNotification();
  worker_.stop();
}

} // namespace
} // namespace Server
} // namespace Envoy
