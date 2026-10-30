#include "envoy/network/exception.h"

#include "source/common/api/api_impl.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/network/address_impl.h"
#include "source/server/worker_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/guard_dog.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/overload_manager.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ElementsAre;
using testing::InSequence;
using testing::NiceMock;
using testing::Optional;
using testing::Ref;
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
      .WillOnce([current_thread_id](std::optional<uint64_t>, Network::ListenerConfig& config,
                                    Runtime::Loader&, Random::RandomGenerator&) -> void {
        EXPECT_EQ(config.listenerTag(), 1UL);
        EXPECT_NE(current_thread_id, std::this_thread::get_id());
      });
  worker_.addListener(
      std::nullopt, listener, [&ci]() -> void { ci.setReady(); }, runtime_, random_);

  NiceMock<Stats::MockStore> store;
  worker_.start(guard_dog_, emptyCallback, std::nullopt);
  worker_.initializeStats(*store.rootScope());
  ci.waitReady();

  // After a worker is started adding/stopping/removing a listener happens on the worker thread.
  NiceMock<Network::MockListenerConfig> listener2;
  ON_CALL(listener2, listenerTag()).WillByDefault(Return(2UL));
  EXPECT_CALL(*handler_, addListener(_, _, _, _))
      .WillOnce([current_thread_id](std::optional<uint64_t>, Network::ListenerConfig& config,
                                    Runtime::Loader&, Random::RandomGenerator&) -> void {
        EXPECT_EQ(config.listenerTag(), 2UL);
        EXPECT_NE(current_thread_id, std::this_thread::get_id());
      });
  worker_.addListener(
      std::nullopt, listener2, [&ci]() -> void { ci.setReady(); }, runtime_, random_);
  ci.waitReady();

  EXPECT_CALL(*handler_, stopListeners(2, _)).WillOnce([current_thread_id, &ci]() -> void {
    EXPECT_NE(current_thread_id, std::this_thread::get_id());
    ci.setReady();
  });

  ConditionalInitializer ci2;
  // Verify that callback is called from the other thread.
  worker_.stopListener(listener2, {}, [current_thread_id, &ci2]() {
    EXPECT_NE(current_thread_id, std::this_thread::get_id());
    ci2.setReady();
  });
  ci.waitReady();
  ci2.waitReady();

  EXPECT_CALL(*handler_, removeListeners(2)).WillOnce([current_thread_id]() -> void {
    EXPECT_NE(current_thread_id, std::this_thread::get_id());
  });
  worker_.removeListener(listener2, [current_thread_id, &ci]() -> void {
    EXPECT_NE(current_thread_id, std::this_thread::get_id());
    ci.setReady();
  });
  ci.waitReady();

  // Now test adding and removing a listener without stopping it first.
  NiceMock<Network::MockListenerConfig> listener3;
  ON_CALL(listener3, listenerTag()).WillByDefault(Return(3UL));
  EXPECT_CALL(*handler_, addListener(_, _, _, _))
      .WillOnce([current_thread_id](std::optional<uint64_t>, Network::ListenerConfig& config,
                                    Runtime::Loader&, Random::RandomGenerator&) -> void {
        EXPECT_EQ(config.listenerTag(), 3UL);
        EXPECT_NE(current_thread_id, std::this_thread::get_id());
      });
  worker_.addListener(
      std::nullopt, listener3, [&ci]() -> void { ci.setReady(); }, runtime_, random_);
  ci.waitReady();

  EXPECT_CALL(*handler_, removeListeners(3)).WillOnce([current_thread_id]() -> void {
    EXPECT_NE(current_thread_id, std::this_thread::get_id());
  });
  worker_.removeListener(listener3, [current_thread_id]() -> void {
    EXPECT_NE(current_thread_id, std::this_thread::get_id());
  });

  worker_.stop();
}

TEST_F(WorkerImplTest, DrainPostsToWorkerThread) {
  InSequence s;
  std::thread::id current_thread_id = std::this_thread::get_id();
  ConditionalInitializer ci;

  NiceMock<Network::MockListenerConfig> listener;
  ON_CALL(listener, listenerTag()).WillByDefault(Return(7UL));
  EXPECT_CALL(*handler_, addListener(_, _, _, _));
  worker_.addListener(std::nullopt, listener, [&ci]() { ci.setReady(); }, runtime_, random_);
  worker_.start(guard_dog_, emptyCallback, std::nullopt);
  ci.waitReady();

  // onListenerDrain posts to the worker dispatcher and invokes handler->onListenerDrain on
  // the worker thread.
  EXPECT_CALL(*handler_, onListenerDrain(7UL, _)).WillOnce([current_thread_id, &ci]() {
    EXPECT_NE(current_thread_id, std::this_thread::get_id());
    ci.setReady();
  });
  worker_.onListenerDrain(7UL, Network::ConnectionDrainEvent{});
  ci.waitReady();

  // onFilterChainDrain likewise posts and forwards on the worker thread.
  const std::list<const Network::FilterChain*> filter_chains;
  EXPECT_CALL(*handler_, onFilterChainDrain(7UL, _, _))
      .WillOnce([current_thread_id, &ci](uint64_t, const std::list<const Network::FilterChain*>&,
                                         Network::ConnectionDrainEvent) {
        EXPECT_NE(current_thread_id, std::this_thread::get_id());
        ci.setReady();
      });
  worker_.onFilterChainDrain(7UL, filter_chains, Network::ConnectionDrainEvent{});
  ci.waitReady();

  worker_.stop();
}

TEST_F(WorkerImplTest, ListenerAddressesPostToWorkerThread) {
  InSequence s;
  std::thread::id current_thread_id = std::this_thread::get_id();
  ConditionalInitializer ci;

  NiceMock<Network::MockListenerConfig> listener;
  EXPECT_CALL(*handler_, addListener(_, _, _, _));
  worker_.addListener(std::nullopt, listener, [&ci]() { ci.setReady(); }, runtime_, random_);
  worker_.start(guard_dog_, emptyCallback, std::nullopt);
  ci.waitReady();

  const Network::Address::InstanceConstSharedPtr address =
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 80);
  bool stopped = false;
  EXPECT_CALL(*handler_, stopListenerAddresses(Ref(listener), ElementsAre(address)))
      .WillOnce([current_thread_id, &stopped]() {
        EXPECT_NE(current_thread_id, std::this_thread::get_id());
        stopped = true;
      });
  // The completion is called on the worker thread after the handler has stopped the addresses.
  worker_.stopListenerAddresses(listener, {address}, [current_thread_id, &stopped, &ci]() {
    EXPECT_NE(current_thread_id, std::this_thread::get_id());
    EXPECT_TRUE(stopped);
    ci.setReady();
  });
  ci.waitReady();

  const Network::ConnectionDrainEvent drain_event{MonotonicTime(std::chrono::seconds(1)),
                                                  Server::DrainStrategy::Immediate};
  EXPECT_CALL(*handler_, onListenerAddressesDrain(Ref(listener), _))
      .WillOnce([current_thread_id, &ci, &drain_event](const Network::ListenerConfig&,
                                                       Network::ConnectionDrainEvent event) {
        EXPECT_NE(current_thread_id, std::this_thread::get_id());
        EXPECT_EQ(drain_event.start_time, event.start_time);
        EXPECT_EQ(drain_event.strategy, event.strategy);
        ci.setReady();
      });
  worker_.onListenerAddressesDrain(listener, drain_event);
  ci.waitReady();

  EXPECT_CALL(*handler_, removeListenerAddresses(Ref(listener)))
      .WillOnce([current_thread_id, &ci]() {
        EXPECT_NE(current_thread_id, std::this_thread::get_id());
        ci.setReady();
      });
  worker_.removeListenerAddresses(listener);
  ci.waitReady();

  worker_.stop();
}

// A worker runs the removal of filter chains before the in place add of a later version of the
// listener posted after it, so the removal only closes the connections accepted before that
// version, even on a filter chain that the version uses again.
TEST_F(WorkerImplTest, RemoveFilterChainsRunsBeforeLaterAddListener) {
  InSequence s;
  ConditionalInitializer ci;
  absl::Notification later_posts_queued;

  NiceMock<Network::MockListenerConfig> listener;
  // The worker waits in the first add until both later posts are queued.
  EXPECT_CALL(*handler_, addListener(_, _, _, _)).WillOnce([&later_posts_queued]() {
    later_posts_queued.WaitForNotification();
  });
  worker_.addListener(std::nullopt, listener, []() {}, runtime_, random_);
  worker_.start(guard_dog_, emptyCallback, std::nullopt);

  const std::list<const Network::FilterChain*> filter_chains;
  EXPECT_CALL(*handler_, removeFilterChains(7UL, _, _));
  EXPECT_CALL(*handler_, addListener(Optional(7UL), _, _, _));
  worker_.removeFilterChains(7UL, filter_chains, []() {});
  worker_.addListener(7UL, listener, [&ci]() { ci.setReady(); }, runtime_, random_);
  later_posts_queued.Notify();
  ci.waitReady();

  worker_.stop();
}

TEST_F(WorkerImplTest, WorkerInvokesProvidedCallback) {
  absl::Notification callback_ran;
  auto cb = [&callback_ran]() { callback_ran.Notify(); };
  worker_.start(guard_dog_, cb, std::nullopt);

  callback_ran.WaitForNotification();
  worker_.stop();
}

class WorkerOverloadTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  WorkerOverloadTest()
      : api_(Api::createApiForTest(simTime())),
        dispatcher_(api_->allocateDispatcher("worker_test")),
        stat_names_(api_->rootScope().symbolTable()) {}

  NiceMock<ThreadLocal::MockInstance> tls_;
  DefaultListenerHooks hooks_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  WorkerStatNames stat_names_;
  NiceMock<MockOverloadManager> overload_manager_;
};

TEST_F(WorkerOverloadTest, CloseIdleHttpConnections) {
  OverloadActionCb captured_cb;
  EXPECT_CALL(overload_manager_, registerForAction(_, _, _))
      .WillRepeatedly([&](const std::string& name, Event::Dispatcher&, OverloadActionCb cb) {
        if (name == OverloadActionNames::get().CloseIdleHttpConnections) {
          captured_cb = cb;
        }
        return true;
      });

  auto* handler = new NiceMock<Network::MockConnectionHandler>();
  auto* dispatcher_ptr = dispatcher_.get();
  WorkerImpl worker(tls_, hooks_, std::move(dispatcher_), Network::ConnectionHandlerPtr{handler},
                    overload_manager_, *api_, stat_names_);

  ASSERT_TRUE(captured_cb != nullptr);

  auto trigger_and_advance = [&](OverloadActionState state, std::chrono::milliseconds duration =
                                                                std::chrono::milliseconds(100)) {
    captured_cb(state);
    simTime().advanceTimeAndRun(duration, *dispatcher_ptr, Event::Dispatcher::RunType::NonBlock);
  };

  // 1. Transition to scaling (active)
  EXPECT_CALL(*handler, closeIdleHttpConnections(false));
  trigger_and_advance(OverloadActionState(UnitFloat(0.5)));

  // 2. Transition to saturated
  EXPECT_CALL(*handler, closeIdleHttpConnections(true));
  trigger_and_advance(OverloadActionState::saturated());

  // 3. Transition back to inactive
  EXPECT_CALL(*handler, closeIdleHttpConnections(_)).Times(0);
  trigger_and_advance(OverloadActionState::inactive());
}

} // namespace
} // namespace Server
} // namespace Envoy
