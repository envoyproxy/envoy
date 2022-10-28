#include <functional>

#include "envoy/common/scope_tracker.h"
#include "envoy/thread/thread.h"

#include "source/common/api/api_impl.h"
#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/lock_guard.h"
#include "source/common/common/scope_tracker.h"
#include "source/common/common/utility.h"
#include "source/common/event/deferred_task.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/event/timer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/watch_dog.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ByMove;
using testing::InSequence;
using testing::MockFunction;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Event {
namespace {

class RunOnDelete {
public:
  RunOnDelete(std::function<void()> on_destroy) : on_destroy_(on_destroy) {}
  ~RunOnDelete() { on_destroy_(); }

private:
  std::function<void()> on_destroy_;
};

void onWatcherReady(evwatch*, const evwatch_prepare_cb_info*, void* arg) {
  // `arg` contains the ReadyWatcher passed in from evwatch_prepare_new.
  auto watcher = static_cast<ReadyWatcher*>(arg);
  watcher->ready();
}

class SchedulableCallbackImplTest : public testing::Test {
protected:
  SchedulableCallbackImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  void createCallback(std::function<void()> cb) {
    callbacks_.emplace_back(dispatcher_->createSchedulableCallback(cb));
  }

  Api::ApiPtr api_;
  DispatcherPtr dispatcher_;
  std::vector<SchedulableCallbackPtr> callbacks_;
};

TEST_F(SchedulableCallbackImplTest, ScheduleCurrentAndCancel) {
  ReadyWatcher watcher;

  auto cb = dispatcher_->createSchedulableCallback([&]() { watcher.ready(); });

  // Cancel is a no-op if not scheduled.
  cb->cancel();
  dispatcher_->run(Dispatcher::RunType::Block);

  // Callback is not invoked if cancelled before it executes.
  cb->scheduleCallbackCurrentIteration();
  EXPECT_TRUE(cb->enabled());
  cb->cancel();
  EXPECT_FALSE(cb->enabled());
  dispatcher_->run(Dispatcher::RunType::Block);

  // Scheduled callback executes.
  cb->scheduleCallbackCurrentIteration();
  EXPECT_CALL(watcher, ready());
  dispatcher_->run(Dispatcher::RunType::Block);

  // Callbacks implicitly cancelled if runner is deleted.
  cb->scheduleCallbackCurrentIteration();
  cb.reset();
  dispatcher_->run(Dispatcher::RunType::Block);
}

TEST_F(SchedulableCallbackImplTest, ScheduleNextAndCancel) {
  ReadyWatcher watcher;

  auto cb = dispatcher_->createSchedulableCallback([&]() { watcher.ready(); });

  // Cancel is a no-op if not scheduled.
  cb->cancel();
  dispatcher_->run(Dispatcher::RunType::Block);

  // Callback is not invoked if cancelled before it executes.
  cb->scheduleCallbackNextIteration();
  EXPECT_TRUE(cb->enabled());
  cb->cancel();
  EXPECT_FALSE(cb->enabled());
  dispatcher_->run(Dispatcher::RunType::Block);

  // Scheduled callback executes.
  cb->scheduleCallbackNextIteration();
  EXPECT_CALL(watcher, ready());
  dispatcher_->run(Dispatcher::RunType::Block);

  // Callbacks implicitly cancelled if runner is deleted.
  cb->scheduleCallbackNextIteration();
  cb.reset();
  dispatcher_->run(Dispatcher::RunType::Block);
}

TEST_F(SchedulableCallbackImplTest, ScheduleOrder) {
  ReadyWatcher watcher0;
  createCallback([&]() { watcher0.ready(); });
  ReadyWatcher watcher1;
  createCallback([&]() { watcher1.ready(); });
  ReadyWatcher watcher2;
  createCallback([&]() { watcher2.ready(); });

  // Current iteration callbacks run in the order they are scheduled. Next iteration callbacks run
  // after current iteration callbacks.
  callbacks_[0]->scheduleCallbackNextIteration();
  callbacks_[1]->scheduleCallbackCurrentIteration();
  callbacks_[2]->scheduleCallbackCurrentIteration();
  InSequence s;
  EXPECT_CALL(watcher1, ready());
  EXPECT_CALL(watcher2, ready());
  EXPECT_CALL(watcher0, ready());
  dispatcher_->run(Dispatcher::RunType::Block);
}

TEST_F(SchedulableCallbackImplTest, ScheduleChainingAndCancellation) {
  DispatcherImpl* dispatcher_impl = static_cast<DispatcherImpl*>(dispatcher_.get());
  ReadyWatcher prepare_watcher;
  evwatch_prepare_new(&dispatcher_impl->base(), onWatcherReady, &prepare_watcher);

  ReadyWatcher watcher0;
  createCallback([&]() {
    watcher0.ready();
    callbacks_[1]->scheduleCallbackCurrentIteration();
  });

  ReadyWatcher watcher1;
  createCallback([&]() {
    watcher1.ready();
    callbacks_[2]->scheduleCallbackCurrentIteration();
    callbacks_[3]->scheduleCallbackCurrentIteration();
    callbacks_[4]->scheduleCallbackCurrentIteration();
    callbacks_[5]->scheduleCallbackNextIteration();
  });

  ReadyWatcher watcher2;
  createCallback([&]() {
    watcher2.ready();
    EXPECT_TRUE(callbacks_[3]->enabled());
    callbacks_[3]->cancel();
    EXPECT_TRUE(callbacks_[4]->enabled());
    callbacks_[4].reset();
  });

  ReadyWatcher watcher3;
  createCallback([&]() { watcher3.ready(); });

  ReadyWatcher watcher4;
  createCallback([&]() { watcher4.ready(); });

  ReadyWatcher watcher5;
  createCallback([&]() { watcher5.ready(); });

  // Chained callbacks run in the same event loop iteration, as signaled by a single call to
  // prepare_watcher.ready(). watcher3 and watcher4 are not invoked because cb2 cancels
  // cb3 and deletes cb4 as part of its execution. cb5 runs after a second call to the
  // prepare callback since it's scheduled for the next iteration.
  callbacks_[0]->scheduleCallbackCurrentIteration();
  InSequence s;
  EXPECT_CALL(prepare_watcher, ready());
  EXPECT_CALL(watcher0, ready());
  EXPECT_CALL(watcher1, ready());
  EXPECT_CALL(watcher2, ready());
  EXPECT_CALL(prepare_watcher, ready());
  EXPECT_CALL(watcher5, ready());
  dispatcher_->run(Dispatcher::RunType::Block);
}

TEST_F(SchedulableCallbackImplTest, RescheduleNext) {
  DispatcherImpl* dispatcher_impl = static_cast<DispatcherImpl*>(dispatcher_.get());
  ReadyWatcher prepare_watcher;
  evwatch_prepare_new(&dispatcher_impl->base(), onWatcherReady, &prepare_watcher);

  ReadyWatcher watcher0;
  createCallback([&]() {
    watcher0.ready();
    // Callback 1 was scheduled from the previous iteration, expect it to fire in the current
    // iteration despite the attempt to reschedule.
    callbacks_[1]->scheduleCallbackNextIteration();
    // Callback 2 expected to execute next iteration because current called before next.
    callbacks_[2]->scheduleCallbackCurrentIteration();
    callbacks_[2]->scheduleCallbackNextIteration();
    // Callback 3 expected to execute next iteration because next was called before current.
    callbacks_[3]->scheduleCallbackNextIteration();
    callbacks_[3]->scheduleCallbackCurrentIteration();
  });

  ReadyWatcher watcher1;
  createCallback([&]() { watcher1.ready(); });
  ReadyWatcher watcher2;
  createCallback([&]() { watcher2.ready(); });
  ReadyWatcher watcher3;
  createCallback([&]() { watcher3.ready(); });

  // Schedule callbacks 0 and 1 outside the loop, both will run in the same iteration of the event
  // loop.
  callbacks_[0]->scheduleCallbackCurrentIteration();
  callbacks_[1]->scheduleCallbackNextIteration();

  InSequence s;
  EXPECT_CALL(prepare_watcher, ready());
  EXPECT_CALL(watcher0, ready());
  EXPECT_CALL(watcher1, ready());
  EXPECT_CALL(watcher2, ready());
  EXPECT_CALL(prepare_watcher, ready());
  EXPECT_CALL(watcher3, ready());
  dispatcher_->run(Dispatcher::RunType::Block);
}

class TestDeferredDeletable : public DeferredDeletable {
public:
  TestDeferredDeletable(std::function<void()> on_destroy) : on_destroy_(on_destroy) {}
  ~TestDeferredDeletable() override { on_destroy_(); }

private:
  std::function<void()> on_destroy_;
};

class TestDispatcherThreadDeletable : public DispatcherThreadDeletable {
public:
  TestDispatcherThreadDeletable(std::function<void()> on_destroy) : on_destroy_(on_destroy) {}
  ~TestDispatcherThreadDeletable() override { on_destroy_(); }

private:
  std::function<void()> on_destroy_;
};

TEST(DeferredDeleteTest, DeferredDelete) {
  InSequence s;
  Api::ApiPtr api = Api::createApiForTest();
  DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  ReadyWatcher watcher1;

  dispatcher->deferredDelete(
      DeferredDeletablePtr{new TestDeferredDeletable([&]() -> void { watcher1.ready(); })});

  // The first one will get deleted inline.
  EXPECT_CALL(watcher1, ready());
  dispatcher->clearDeferredDeleteList();

  // This one does a nested deferred delete. We should need two clear calls to actually get
  // rid of it with the vector swapping. We also test that inline clear() call does nothing.
  ReadyWatcher watcher2;
  ReadyWatcher watcher3;
  dispatcher->deferredDelete(DeferredDeletablePtr{new TestDeferredDeletable([&]() -> void {
    watcher2.ready();
    dispatcher->deferredDelete(
        DeferredDeletablePtr{new TestDeferredDeletable([&]() -> void { watcher3.ready(); })});
    dispatcher->clearDeferredDeleteList();
  })});

  EXPECT_CALL(watcher2, ready());
  dispatcher->clearDeferredDeleteList();

  EXPECT_CALL(watcher3, ready());
  dispatcher->clearDeferredDeleteList();
}

TEST(DeferredTaskTest, DeferredTask) {
  InSequence s;
  Api::ApiPtr api = Api::createApiForTest();
  DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  ReadyWatcher watcher1;

  DeferredTaskUtil::deferredRun(*dispatcher, [&watcher1]() -> void { watcher1.ready(); });
  // The first one will get deleted inline.
  EXPECT_CALL(watcher1, ready());
  dispatcher->clearDeferredDeleteList();

  // Deferred task is scheduled FIFO.
  ReadyWatcher watcher2;
  ReadyWatcher watcher3;
  DeferredTaskUtil::deferredRun(*dispatcher, [&watcher2]() -> void { watcher2.ready(); });
  DeferredTaskUtil::deferredRun(*dispatcher, [&watcher3]() -> void { watcher3.ready(); });
  EXPECT_CALL(watcher2, ready());
  EXPECT_CALL(watcher3, ready());
  dispatcher->clearDeferredDeleteList();
}

TEST(DeferredDeleteTest, DeferredDeleteAndPostOrdering) {
  InSequence s;

  Api::ApiPtr api = Api::createApiForTest();
  DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  ReadyWatcher post_watcher;
  ReadyWatcher delete_watcher;

  // DeferredDelete should always run before post callbacks.
  EXPECT_CALL(delete_watcher, ready());
  EXPECT_CALL(post_watcher, ready());

  dispatcher->post([&]() { post_watcher.ready(); });
  dispatcher->deferredDelete(
      std::make_unique<TestDeferredDeletable>([&]() -> void { delete_watcher.ready(); }));
  dispatcher->run(Dispatcher::RunType::NonBlock);
}

class DispatcherImplTest : public testing::Test {
protected:
  DispatcherImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {
    dispatcher_thread_ = api_->threadFactory().createThread([this]() {
      // Must create a keepalive timer to keep the dispatcher from exiting.
      std::chrono::milliseconds time_interval(500);
      keepalive_timer_ = dispatcher_->createTimer(
          [this, time_interval]() { keepalive_timer_->enableTimer(time_interval); });
      keepalive_timer_->enableTimer(time_interval);

      dispatcher_->run(Dispatcher::RunType::Block);
    });
  }

  ~DispatcherImplTest() override {
    dispatcher_->exit();
    dispatcher_thread_->join();
  }

  void timerTest(std::function<void(Timer&)> enable_timer_delegate) {
    {
      Thread::LockGuard lock(mu_);
      work_finished_ = false;
    }
    TimerPtr timer;
    dispatcher_->post([this, &timer, enable_timer_delegate]() {
      {
        Thread::LockGuard lock(mu_);
        timer = dispatcher_->createTimer([this]() {
          {
            Thread::LockGuard lock(mu_);
            ASSERT(!work_finished_);
            work_finished_ = true;
          }
          cv_.notifyOne();
        });
        EXPECT_FALSE(timer->enabled());
        enable_timer_delegate(*timer);
        EXPECT_TRUE(timer->enabled());
      }
    });

    Thread::LockGuard lock(mu_);
    while (!work_finished_) {
      cv_.wait(mu_);
    }
  }

  NiceMock<Stats::MockStore> scope_; // Used in InitializeStats, must outlive dispatcher_->exit().
  Api::ApiPtr api_;
  Thread::ThreadPtr dispatcher_thread_;
  DispatcherPtr dispatcher_;
  Thread::MutexBasicLockable mu_;
  Thread::CondVar cv_;

  bool work_finished_{false};
  TimerPtr keepalive_timer_;
};

// TODO(mergeconflict): We also need integration testing to validate that the expected histograms
// are written when `enable_dispatcher_stats` is true. See issue #6582.
TEST_F(DispatcherImplTest, InitializeStats) {
  EXPECT_CALL(scope_,
              histogram("test.dispatcher.loop_duration_us", Stats::Histogram::Unit::Microseconds));
  EXPECT_CALL(scope_,
              histogram("test.dispatcher.poll_delay_us", Stats::Histogram::Unit::Microseconds));
  dispatcher_->initializeStats(scope_, "test.");
}

TEST_F(DispatcherImplTest, Post) {
  dispatcher_->post([this]() {
    {
      Thread::LockGuard lock(mu_);
      ASSERT(!work_finished_);
      work_finished_ = true;
    }
    cv_.notifyOne();
  });

  Thread::LockGuard lock(mu_);
  while (!work_finished_) {
    cv_.wait(mu_);
  }
}

TEST_F(DispatcherImplTest, PostExecuteAndDestructOrder) {
  ReadyWatcher parent_watcher;
  ReadyWatcher deferred_delete_watcher;
  ReadyWatcher run_watcher1;
  ReadyWatcher delete_watcher1;
  ReadyWatcher run_watcher2;
  ReadyWatcher delete_watcher2;

  // Expect the following events to happen in order. The destructor of the post callback should run
  // before execution of the next post callback starts. The post callback runner should yield after
  // running each group of callbacks in a chain, so the deferred deletion should run before the
  // post callbacks that are also scheduled by the parent post callback.
  InSequence s;
  EXPECT_CALL(parent_watcher, ready());
  EXPECT_CALL(deferred_delete_watcher, ready());
  EXPECT_CALL(run_watcher1, ready());
  EXPECT_CALL(delete_watcher1, ready());
  EXPECT_CALL(run_watcher2, ready());
  EXPECT_CALL(delete_watcher2, ready());

  dispatcher_->post([&]() {
    parent_watcher.ready();
    auto on_delete_task1 =
        std::make_shared<RunOnDelete>([&delete_watcher1]() { delete_watcher1.ready(); });
    dispatcher_->post([&run_watcher1, on_delete_task1]() { run_watcher1.ready(); });
    auto on_delete_task2 =
        std::make_shared<RunOnDelete>([&delete_watcher2]() { delete_watcher2.ready(); });
    dispatcher_->post([&run_watcher2, on_delete_task2]() { run_watcher2.ready(); });
    dispatcher_->post([this]() {
      {
        Thread::LockGuard lock(mu_);
        ASSERT(!work_finished_);
        work_finished_ = true;
      }
      cv_.notifyOne();
    });
    dispatcher_->deferredDelete(std::make_unique<TestDeferredDeletable>(
        [&deferred_delete_watcher]() -> void { deferred_delete_watcher.ready(); }));
  });

  Thread::LockGuard lock(mu_);
  while (!work_finished_) {
    cv_.wait(mu_);
  }
}

// Ensure that there is no deadlock related to calling a posted callback, or
// destructing a closure when finished calling it.
TEST_F(DispatcherImplTest, RunPostCallbacksLocking) {
  class PostOnDestruct {
  public:
    PostOnDestruct(Dispatcher& dispatcher) : dispatcher_(dispatcher) {}
    ~PostOnDestruct() {
      dispatcher_.post([]() {});
    }
    void method() {}
    Dispatcher& dispatcher_;
  };

  {
    // Block dispatcher first to ensure that both posted events below are handled
    // by a single call to runPostCallbacks().
    //
    // This also ensures that the post_lock_ is not held while callbacks are called,
    // or else this would deadlock.
    Thread::LockGuard lock(mu_);
    dispatcher_->post([this]() { Thread::LockGuard lock(mu_); });

    auto post_on_destruct = std::make_shared<PostOnDestruct>(*dispatcher_);
    dispatcher_->post([=]() { post_on_destruct->method(); });
    dispatcher_->post([this]() {
      {
        Thread::LockGuard lock(mu_);
        ASSERT(!work_finished_);
        work_finished_ = true;
      }
      cv_.notifyOne();
    });
  }

  Thread::LockGuard lock(mu_);
  while (!work_finished_) {
    cv_.wait(mu_);
  }
}

TEST_F(DispatcherImplTest, DispatcherThreadDeleted) {
  dispatcher_->deleteInDispatcherThread(std::make_unique<TestDispatcherThreadDeletable>(
      [this, id = api_->threadFactory().currentThreadId()]() {
        ASSERT(id != api_->threadFactory().currentThreadId());
        {
          Thread::LockGuard lock(mu_);
          ASSERT(!work_finished_);
          work_finished_ = true;
        }
        cv_.notifyOne();
      }));

  Thread::LockGuard lock(mu_);
  while (!work_finished_) {
    cv_.wait(mu_);
  }
}

TEST(DispatcherThreadDeletedImplTest, DispatcherThreadDeletedAtNextCycle) {
  Api::ApiPtr api_(Api::createApiForTest());
  DispatcherPtr dispatcher(api_->allocateDispatcher("test_thread"));
  std::vector<std::unique_ptr<ReadyWatcher>> watchers;
  watchers.reserve(3);
  for (int i = 0; i < 3; ++i) {
    watchers.push_back(std::make_unique<ReadyWatcher>());
  }
  dispatcher->deleteInDispatcherThread(
      std::make_unique<TestDispatcherThreadDeletable>([&watchers]() { watchers[0]->ready(); }));
  EXPECT_CALL(*watchers[0], ready());
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
  dispatcher->deleteInDispatcherThread(
      std::make_unique<TestDispatcherThreadDeletable>([&watchers]() { watchers[1]->ready(); }));
  dispatcher->deleteInDispatcherThread(
      std::make_unique<TestDispatcherThreadDeletable>([&watchers]() { watchers[2]->ready(); }));
  EXPECT_CALL(*watchers[1], ready());
  EXPECT_CALL(*watchers[2], ready());
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);
}

class DispatcherShutdownTest : public testing::Test {
protected:
  DispatcherShutdownTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  Api::ApiPtr api_;
  DispatcherPtr dispatcher_;
};

TEST_F(DispatcherShutdownTest, ShutdownClearThreadLocalDeletables) {
  ReadyWatcher watcher;

  dispatcher_->deleteInDispatcherThread(
      std::make_unique<TestDispatcherThreadDeletable>([&watcher]() { watcher.ready(); }));
  EXPECT_CALL(watcher, ready());
  dispatcher_->shutdown();
}

TEST_F(DispatcherShutdownTest, ShutdownDoesnotClearDeferredListOrPostCallback) {
  ReadyWatcher watcher;
  ReadyWatcher deferred_watcher;
  ReadyWatcher post_watcher;

  {
    InSequence s;

    dispatcher_->deferredDelete(std::make_unique<TestDeferredDeletable>(
        [&deferred_watcher]() { deferred_watcher.ready(); }));
    dispatcher_->post([&post_watcher]() { post_watcher.ready(); });
    dispatcher_->deleteInDispatcherThread(
        std::make_unique<TestDispatcherThreadDeletable>([&watcher]() { watcher.ready(); }));
    EXPECT_CALL(watcher, ready());
    dispatcher_->shutdown();

    ::testing::Mock::VerifyAndClearExpectations(&watcher);
    EXPECT_CALL(deferred_watcher, ready());
    dispatcher_.reset();
  }
}

TEST_F(DispatcherShutdownTest, DestroyClearAllList) {
  ReadyWatcher watcher;
  ReadyWatcher deferred_watcher;
  dispatcher_->deferredDelete(
      std::make_unique<TestDeferredDeletable>([&deferred_watcher]() { deferred_watcher.ready(); }));
  dispatcher_->deleteInDispatcherThread(
      std::make_unique<TestDispatcherThreadDeletable>([&watcher]() { watcher.ready(); }));
  {
    InSequence s;
    EXPECT_CALL(deferred_watcher, ready());
    EXPECT_CALL(watcher, ready());
    dispatcher_.reset();
  }
}

TEST_F(DispatcherImplTest, Timer) {
  timerTest([](Timer& timer) { timer.enableTimer(std::chrono::milliseconds(0)); });
  timerTest([](Timer& timer) { timer.enableTimer(std::chrono::milliseconds(50)); });
  timerTest([](Timer& timer) { timer.enableHRTimer(std::chrono::microseconds(50)); });
}

TEST_F(DispatcherImplTest, TimerWithScope) {
  TimerPtr timer;
  MockScopeTrackedObject scope;
  dispatcher_->post([this, &timer, &scope]() {
    {
      // Expect a call to dumpState. The timer will call onFatalError during
      // the alarm interval, and if the scope is tracked correctly this will
      // result in a dumpState call.
      EXPECT_CALL(scope, dumpState(_, _));
      Thread::LockGuard lock(mu_);
      timer = dispatcher_->createTimer([this]() {
        {
          Thread::LockGuard lock(mu_);
          static_cast<DispatcherImpl*>(dispatcher_.get())->onFatalError(std::cerr);
          ASSERT(!work_finished_);
          work_finished_ = true;
        }
        cv_.notifyOne();
      });
      EXPECT_FALSE(timer->enabled());
      timer->enableTimer(std::chrono::milliseconds(50), &scope);
      EXPECT_TRUE(timer->enabled());
    }
  });

  Thread::LockGuard lock(mu_);
  while (!work_finished_) {
    cv_.wait(mu_);
  }
}

TEST_F(DispatcherImplTest, IsThreadSafe) {
  dispatcher_->post([this]() {
    {
      Thread::LockGuard lock(mu_);
      // Thread safe because it is called within the dispatcher thread's context.
      EXPECT_TRUE(dispatcher_->isThreadSafe());
      ASSERT(!work_finished_);
      work_finished_ = true;
    }
    cv_.notifyOne();
  });

  Thread::LockGuard lock(mu_);
  while (!work_finished_) {
    cv_.wait(mu_);
  }
  // Not thread safe because it is not called within the dispatcher thread's context.
  EXPECT_FALSE(dispatcher_->isThreadSafe());
}

TEST_F(DispatcherImplTest, ShouldDumpNothingIfNoTrackedObjects) {
  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};

  // Call on FatalError to trigger dumps of tracked objects.
  dispatcher_->post([this, &ostream]() {
    Thread::LockGuard lock(mu_);
    static_cast<DispatcherImpl*>(dispatcher_.get())->onFatalError(ostream);
    work_finished_ = true;
    cv_.notifyOne();
  });

  Thread::LockGuard lock(mu_);
  while (!work_finished_) {
    cv_.wait(mu_);
  }

  // Check ostream still empty.
  EXPECT_EQ(ostream.contents(), "");
}

TEST_F(DispatcherImplTest, ShouldDumpTrackedObjectsInFILO) {
  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};

  // Call on FatalError to trigger dumps of tracked objects.
  dispatcher_->post([this, &ostream]() {
    Thread::LockGuard lock(mu_);

    // Add several tracked objects to the dispatcher
    MessageTrackedObject first{"first"};
    ScopeTrackerScopeState first_state{&first, *dispatcher_};
    MessageTrackedObject second{"second"};
    ScopeTrackerScopeState second_state{&second, *dispatcher_};
    MessageTrackedObject third{"third"};
    ScopeTrackerScopeState third_state{&third, *dispatcher_};

    static_cast<DispatcherImpl*>(dispatcher_.get())->onFatalError(ostream);
    work_finished_ = true;
    cv_.notifyOne();
  });

  Thread::LockGuard lock(mu_);
  while (!work_finished_) {
    cv_.wait(mu_);
  }

  // Check the dump includes and registered objects in a FILO order.
  EXPECT_EQ(ostream.contents(), "thirdsecondfirst");
}

TEST_F(DispatcherImplTest, TracksIfTrackedObjectStackEmpty) {
  // Post on the dispatcher thread.
  dispatcher_->post([this]() {
    Thread::LockGuard lock(mu_);

    // Initially should be empty
    ASSERT_TRUE(dispatcher_->trackedObjectStackIsEmpty());

    // Add Tracked Object
    {
      MessageTrackedObject first{"first"};
      ScopeTrackerScopeState first_state{&first, *dispatcher_};

      EXPECT_FALSE(dispatcher_->trackedObjectStackIsEmpty());
    }

    // Should be empty now
    EXPECT_TRUE(dispatcher_->trackedObjectStackIsEmpty());

    work_finished_ = true;
    cv_.notifyOne();
  });

  Thread::LockGuard lock(mu_);
  while (!work_finished_) {
    cv_.wait(mu_);
  }
}

class TestFatalAction : public Server::Configuration::FatalAction {
public:
  void run(absl::Span<const ScopeTrackedObject* const> /*tracked_objects*/) override {
    ++times_ran_;
  }
  bool isAsyncSignalSafe() const override { return true; }
  int getNumTimesRan() { return times_ran_; }

private:
  int times_ran_ = 0;
};

TEST_F(DispatcherImplTest, OnlyRunsFatalActionsIfRunningOnSameThread) {
  FatalAction::FatalActionPtrList actions;
  actions.emplace_back(std::make_unique<TestFatalAction>());
  auto* action = dynamic_cast<TestFatalAction*>(actions.front().get());

  ASSERT_EQ(action->getNumTimesRan(), 0);

  // Should not run as dispatcher isn't running yet
  auto non_running_dispatcher = api_->allocateDispatcher("non_running_thread");
  static_cast<DispatcherImpl*>(non_running_dispatcher.get())
      ->runFatalActionsOnTrackedObject(actions);
  ASSERT_EQ(action->getNumTimesRan(), 0);

  // Should not run when not on same thread
  static_cast<DispatcherImpl*>(dispatcher_.get())->runFatalActionsOnTrackedObject(actions);
  ASSERT_EQ(action->getNumTimesRan(), 0);

  // Should run since on same thread as dispatcher
  dispatcher_->post([this, &actions]() {
    {
      Thread::LockGuard lock(mu_);
      static_cast<DispatcherImpl*>(dispatcher_.get())->runFatalActionsOnTrackedObject(actions);
      ASSERT(!work_finished_);
      work_finished_ = true;
    }
    cv_.notifyOne();
  });

  Thread::LockGuard lock(mu_);
  while (!work_finished_) {
    cv_.wait(mu_);
  }

  EXPECT_EQ(action->getNumTimesRan(), 1);
}

class NotStartedDispatcherImplTest : public testing::Test {
protected:
  NotStartedDispatcherImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  Api::ApiPtr api_;
  DispatcherPtr dispatcher_;
};

TEST_F(NotStartedDispatcherImplTest, IsThreadSafe) {
  // Thread safe because the dispatcher has not started.
  // Therefore, no thread id has been assigned.
  EXPECT_TRUE(dispatcher_->isThreadSafe());
}

class DispatcherMonotonicTimeTest : public testing::Test {
protected:
  DispatcherMonotonicTimeTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}
  ~DispatcherMonotonicTimeTest() override = default;

  Api::ApiPtr api_;
  DispatcherPtr dispatcher_;
  MonotonicTime time_;
};

TEST_F(DispatcherMonotonicTimeTest, UpdateApproximateMonotonicTime) {
  dispatcher_->post([this]() {
    {
      MonotonicTime time1 = dispatcher_->approximateMonotonicTime();
      dispatcher_->updateApproximateMonotonicTime();
      MonotonicTime time2 = dispatcher_->approximateMonotonicTime();
      EXPECT_LT(time1, time2);
    }
  });

  dispatcher_->run(Dispatcher::RunType::Block);
}

TEST_F(DispatcherMonotonicTimeTest, ApproximateMonotonicTime) {
  // approximateMonotonicTime is constant within one event loop run.
  dispatcher_->post([this]() {
    {
      time_ = dispatcher_->approximateMonotonicTime();
      EXPECT_EQ(time_, dispatcher_->approximateMonotonicTime());
    }
  });

  dispatcher_->run(Dispatcher::RunType::Block);

  // approximateMonotonicTime is increasing between event loop runs.
  dispatcher_->post([this]() {
    { EXPECT_LT(time_, dispatcher_->approximateMonotonicTime()); }
  });

  dispatcher_->run(Dispatcher::RunType::Block);
}

class TimerImplTest : public testing::Test {
protected:
  TimerImplTest() {
    // Hook into event loop prepare and check events.
    evwatch_prepare_new(&libevent_base_, onWatcherReady, &prepare_watcher_);
    evwatch_check_new(&libevent_base_, onCheck, this);
  }
  ~TimerImplTest() override { ASSERT(check_callbacks_.empty()); }

  // Run a callback inside the event loop. The libevent monotonic time used for timer registration
  // is frozen while within this callback, so timers enabled within this callback end up with the
  // requested relative registration times. The callback can invoke advanceLibeventTime() to force
  // the libevent monotonic time forward before libevent determines the list of triggered timers.
  void runInEventLoop(std::function<void()> cb) {
    check_callbacks_.emplace_back(cb);

    // Add a callback to the event loop to force it to run at least once despite there being no
    // registered timers yet.
    auto callback = dispatcher_->createSchedulableCallback([]() {});
    callback->scheduleCallbackCurrentIteration();

    in_event_loop_ = true;
    dispatcher_->run(Dispatcher::RunType::NonBlock);
    in_event_loop_ = false;
  }

  // Advance time forward while updating the libevent's time cache and monotonic time reference.
  // Pushing the monotonic time reference forward eliminates the possibility of time moving
  // backwards and breaking the overly picky TimerImpl tests below.
  void advanceLibeventTime(absl::Duration duration) {
    ASSERT(in_event_loop_);
    requested_advance_ += duration;
    adjustCachedTime();
  }

  // Similar to advanceLibeventTime, but for use in mock callback actions. Monotonic time will be
  // moved forward at the start of the next event loop iteration.
  void advanceLibeventTimeNextIteration(absl::Duration duration) {
    ASSERT(in_event_loop_);
    requested_advance_ += duration;
  }

  Api::ApiPtr api_{Api::createApiForTest()};
  DispatcherPtr dispatcher_{api_->allocateDispatcher("test_thread")};
  event_base& libevent_base_{static_cast<DispatcherImpl&>(*dispatcher_).base()};
  ReadyWatcher prepare_watcher_;
  std::vector<SchedulableCallbackPtr> callbacks_;

private:
  static void onCheck(evwatch*, const evwatch_check_cb_info*, void* arg) {
    // `arg` contains the TimerImplTest passed in from evwatch_check_new.
    auto self = static_cast<TimerImplTest*>(arg);
    auto check_callbacks = self->check_callbacks_;
    self->check_callbacks_.clear();
    for (const auto& cb : check_callbacks) {
      cb();
    }
    self->adjustCachedTime();
  }

  absl::Duration cachedTimeAsDuration() const {
    timeval tv;
    int ret = event_base_gettimeofday_cached(&libevent_base_, &tv);
    RELEASE_ASSERT(ret == 0, "event_base_gettimeofday_cached failed");
    return absl::DurationFromTimeval(tv);
  }

  void adjustCachedTime() {
    auto start = cachedTimeAsDuration();
    // Sanity check: ensure that cache time is in use.
    EXPECT_EQ(start, cachedTimeAsDuration());

    while (cachedTimeAsDuration() - start < requested_advance_) {
      absl::SleepFor(absl::Milliseconds(1));
      event_base_update_cache_time(&libevent_base_);
    }
    requested_advance_ = absl::ZeroDuration();
  }

  absl::Duration requested_advance_ = absl::ZeroDuration();
  std::vector<std::function<void()>> check_callbacks_;
  bool in_event_loop_{};
};

TEST_F(TimerImplTest, TimerEnabledDisabled) {
  InSequence s;

  Event::TimerPtr timer = dispatcher_->createTimer([] {});
  EXPECT_FALSE(timer->enabled());
  timer->enableTimer(std::chrono::milliseconds(0));
  EXPECT_TRUE(timer->enabled());
  EXPECT_CALL(prepare_watcher_, ready());
  dispatcher_->run(Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(timer->enabled());
  timer->enableHRTimer(std::chrono::milliseconds(0));
  EXPECT_TRUE(timer->enabled());
  EXPECT_CALL(prepare_watcher_, ready());
  dispatcher_->run(Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(timer->enabled());
}

TEST_F(TimerImplTest, ChangeTimerBackwardsBeforeRun) {
  ReadyWatcher watcher1;
  Event::TimerPtr timer1 = dispatcher_->createTimer([&] { watcher1.ready(); });

  ReadyWatcher watcher2;
  Event::TimerPtr timer2 = dispatcher_->createTimer([&] { watcher2.ready(); });

  ReadyWatcher watcher3;
  Event::TimerPtr timer3 = dispatcher_->createTimer([&] { watcher3.ready(); });

  // Expect watcher3 to trigger first because the deadlines for timers 1 and 2 was moved backwards.
  InSequence s;
  EXPECT_CALL(prepare_watcher_, ready());
  EXPECT_CALL(watcher3, ready());
  EXPECT_CALL(watcher2, ready());
  EXPECT_CALL(watcher1, ready());
  runInEventLoop([&]() {
    timer1->enableTimer(std::chrono::milliseconds(0));
    timer2->enableTimer(std::chrono::milliseconds(1));
    timer3->enableTimer(std::chrono::milliseconds(2));
    timer2->enableTimer(std::chrono::milliseconds(3));
    timer1->enableTimer(std::chrono::milliseconds(4));

    // Advance time by 10ms so timers above all trigger in the same loop iteration.
    advanceLibeventTime(absl::Milliseconds(10));
  });
}

TEST_F(TimerImplTest, ChangeTimerForwardsToZeroBeforeRun) {
  ReadyWatcher watcher1;
  Event::TimerPtr timer1 = dispatcher_->createTimer([&] { watcher1.ready(); });

  ReadyWatcher watcher2;
  Event::TimerPtr timer2 = dispatcher_->createTimer([&] { watcher2.ready(); });

  // Expect watcher1 to trigger first because timer1's deadline was moved forward.
  InSequence s;
  EXPECT_CALL(prepare_watcher_, ready());
  EXPECT_CALL(watcher1, ready());
  EXPECT_CALL(watcher2, ready());
  runInEventLoop([&]() {
    timer1->enableTimer(std::chrono::milliseconds(2));
    timer2->enableTimer(std::chrono::milliseconds(1));
    timer1->enableTimer(std::chrono::milliseconds(0));

    // Advance time by 10ms so timers above all trigger in the same loop iteration.
    advanceLibeventTime(absl::Milliseconds(10));
  });
}

TEST_F(TimerImplTest, ChangeTimerForwardsToNonZeroBeforeRun) {
  ReadyWatcher watcher1;
  Event::TimerPtr timer1 = dispatcher_->createTimer([&] { watcher1.ready(); });

  ReadyWatcher watcher2;
  Event::TimerPtr timer2 = dispatcher_->createTimer([&] { watcher2.ready(); });

  // Expect watcher1 to trigger first because timer1's deadline was moved forward.
  InSequence s;
  EXPECT_CALL(prepare_watcher_, ready());
  EXPECT_CALL(watcher1, ready());
  EXPECT_CALL(watcher2, ready());
  runInEventLoop([&]() {
    timer1->enableTimer(std::chrono::milliseconds(3));
    timer2->enableTimer(std::chrono::milliseconds(2));
    timer1->enableTimer(std::chrono::milliseconds(1));

    // Advance time by 10ms so timers above all trigger in the same loop iteration.
    advanceLibeventTime(absl::Milliseconds(10));
  });
}

TEST_F(TimerImplTest, ChangeLargeTimerForwardToZeroBeforeRun) {
  ReadyWatcher watcher1;
  Event::TimerPtr timer1 = dispatcher_->createTimer([&] { watcher1.ready(); });

  ReadyWatcher watcher2;
  Event::TimerPtr timer2 = dispatcher_->createTimer([&] { watcher2.ready(); });

  // Expect watcher1 to trigger because timer1's deadline was moved forward.
  InSequence s;
  EXPECT_CALL(prepare_watcher_, ready());
  EXPECT_CALL(watcher1, ready());
  EXPECT_CALL(prepare_watcher_, ready());
  runInEventLoop([&]() {
    timer1->enableTimer(std::chrono::seconds(2000));
    timer2->enableTimer(std::chrono::seconds(1000));
    timer1->enableTimer(std::chrono::seconds(0));
  });
}

TEST_F(TimerImplTest, ChangeLargeTimerForwardToNonZeroBeforeRun) {
  ReadyWatcher watcher1;
  Event::TimerPtr timer1 = dispatcher_->createTimer([&] { watcher1.ready(); });

  ReadyWatcher watcher2;
  Event::TimerPtr timer2 = dispatcher_->createTimer([&] { watcher2.ready(); });

  // Expect watcher1 to trigger because timer1's deadline was moved forward.
  InSequence s;
  EXPECT_CALL(prepare_watcher_, ready());
  EXPECT_CALL(watcher1, ready());
  EXPECT_CALL(prepare_watcher_, ready());
  runInEventLoop([&]() {
    timer1->enableTimer(std::chrono::seconds(2000));
    timer2->enableTimer(std::chrono::seconds(1000));
    timer1->enableTimer(std::chrono::milliseconds(1));

    // Advance time by 10ms so timers above all trigger in the same loop iteration.
    advanceLibeventTime(absl::Milliseconds(10));
  });
}

// Timers scheduled at different times execute in order.
TEST_F(TimerImplTest, TimerOrdering) {
  ReadyWatcher watcher1;
  Event::TimerPtr timer1 = dispatcher_->createTimer([&] { watcher1.ready(); });

  ReadyWatcher watcher2;
  Event::TimerPtr timer2 = dispatcher_->createTimer([&] { watcher2.ready(); });

  ReadyWatcher watcher3;
  Event::TimerPtr timer3 = dispatcher_->createTimer([&] { watcher3.ready(); });

  // Expect watcher calls to happen in order since timers have different times.
  InSequence s;
  EXPECT_CALL(prepare_watcher_, ready());
  EXPECT_CALL(watcher1, ready());
  EXPECT_CALL(watcher2, ready());
  EXPECT_CALL(watcher3, ready());

  runInEventLoop([&]() {
    timer1->enableTimer(std::chrono::milliseconds(0));
    timer2->enableTimer(std::chrono::milliseconds(1));
    timer3->enableTimer(std::chrono::milliseconds(2));

    // Advance time by 10ms so timers above all trigger in the same loop iteration.
    advanceLibeventTime(absl::Milliseconds(10));

    EXPECT_TRUE(timer1->enabled());
    EXPECT_TRUE(timer2->enabled());
    EXPECT_TRUE(timer3->enabled());
  });
}

// Alarms that are scheduled to execute and are cancelled do not trigger.
TEST_F(TimerImplTest, TimerOrderAndDisableAlarm) {
  ReadyWatcher watcher3;
  Event::TimerPtr timer3 = dispatcher_->createTimer([&] { watcher3.ready(); });

  ReadyWatcher watcher2;
  Event::TimerPtr timer2 = dispatcher_->createTimer([&] { watcher2.ready(); });

  ReadyWatcher watcher1;
  Event::TimerPtr timer1 = dispatcher_->createTimer([&] {
    timer2->disableTimer();
    watcher1.ready();
  });

  // Expect watcher calls to happen in order since timers have different times.
  InSequence s;
  EXPECT_CALL(prepare_watcher_, ready());
  EXPECT_CALL(watcher1, ready());
  EXPECT_CALL(watcher3, ready());
  runInEventLoop([&]() {
    timer1->enableTimer(std::chrono::milliseconds(0));
    timer2->enableTimer(std::chrono::milliseconds(1));
    timer3->enableTimer(std::chrono::milliseconds(2));

    // Advance time by 10ms so timers above all trigger in the same loop iteration.
    advanceLibeventTime(absl::Milliseconds(10));

    EXPECT_TRUE(timer1->enabled());
    EXPECT_TRUE(timer2->enabled());
    EXPECT_TRUE(timer3->enabled());
  });
}

// Change the registration time for a timer that is already activated by disabling and re-enabling
// the timer. Verify that execution is delayed.
TEST_F(TimerImplTest, TimerOrderDisableAndReschedule) {
  ReadyWatcher watcher4;
  Event::TimerPtr timer4 = dispatcher_->createTimer([&] { watcher4.ready(); });

  ReadyWatcher watcher3;
  Event::TimerPtr timer3 = dispatcher_->createTimer([&] { watcher3.ready(); });

  ReadyWatcher watcher2;
  Event::TimerPtr timer2 = dispatcher_->createTimer([&] { watcher2.ready(); });

  ReadyWatcher watcher1;
  Event::TimerPtr timer1 = dispatcher_->createTimer([&] {
    timer2->disableTimer();
    timer2->enableTimer(std::chrono::milliseconds(0));
    timer3->disableTimer();
    timer3->enableTimer(std::chrono::milliseconds(1));
    watcher1.ready();
  });

  // timer1 is expected to run first and reschedule timers 2 and 3. timer4 should fire before
  // timer2 and timer3 since timer4's registration is unaffected.
  InSequence s;
  EXPECT_CALL(prepare_watcher_, ready());
  EXPECT_CALL(watcher1, ready());
  EXPECT_CALL(watcher4, ready());
  // Sleep during prepare to ensure that enough time has elapsed before timer evaluation to ensure
  // that timers 2 and 3 are picked up by the same loop iteration. Without the sleep the two
  // timers could execute in different loop iterations.
  EXPECT_CALL(prepare_watcher_, ready()).WillOnce(testing::InvokeWithoutArgs([&]() {
    advanceLibeventTimeNextIteration(absl::Milliseconds(10));
  }));
  EXPECT_CALL(watcher2, ready());
  EXPECT_CALL(watcher3, ready());
  runInEventLoop([&]() {
    timer1->enableTimer(std::chrono::milliseconds(0));
    timer2->enableTimer(std::chrono::milliseconds(1));
    timer3->enableTimer(std::chrono::milliseconds(2));
    timer4->enableTimer(std::chrono::milliseconds(3));

    // Advance time by 10ms so timers above all trigger in the same loop iteration.
    advanceLibeventTime(absl::Milliseconds(10));

    EXPECT_TRUE(timer1->enabled());
    EXPECT_TRUE(timer2->enabled());
    EXPECT_TRUE(timer3->enabled());
    EXPECT_TRUE(timer4->enabled());
  });
}

// Change the registration time for a timer that is already activated by re-enabling the timer
// without calling disableTimer first.
TEST_F(TimerImplTest, TimerOrderAndReschedule) {
  ReadyWatcher watcher4;
  Event::TimerPtr timer4 = dispatcher_->createTimer([&] { watcher4.ready(); });

  ReadyWatcher watcher3;
  Event::TimerPtr timer3 = dispatcher_->createTimer([&] { watcher3.ready(); });

  ReadyWatcher watcher2;
  Event::TimerPtr timer2 = dispatcher_->createTimer([&] { watcher2.ready(); });

  ReadyWatcher watcher1;
  Event::TimerPtr timer1 = dispatcher_->createTimer([&] {
    timer2->enableTimer(std::chrono::milliseconds(0));
    timer3->enableTimer(std::chrono::milliseconds(1));
    watcher1.ready();
  });

  // Rescheduling timers that are already scheduled to run in the current event loop iteration has
  // no effect if the time delta is 0. Expect timers 1, 2 and 4 to execute in the original order.
  // Timer 3 is delayed since it is rescheduled with a non-zero delta.
  InSequence s;
  EXPECT_CALL(prepare_watcher_, ready());
  EXPECT_CALL(watcher1, ready());
  EXPECT_CALL(watcher4, ready());
  // Sleep during prepare to ensure that enough time has elapsed before timer evaluation to ensure
  // that timers 2 and 3 are picked up by the same loop iteration. Without the sleep the two
  // timers could execute in different loop iterations.
  EXPECT_CALL(prepare_watcher_, ready()).WillOnce(testing::InvokeWithoutArgs([&]() {
    advanceLibeventTimeNextIteration(absl::Milliseconds(10));
  }));
  EXPECT_CALL(watcher2, ready());
  EXPECT_CALL(watcher3, ready());
  runInEventLoop([&]() {
    timer1->enableTimer(std::chrono::milliseconds(0));
    timer2->enableTimer(std::chrono::milliseconds(1));
    timer3->enableTimer(std::chrono::milliseconds(2));
    timer4->enableTimer(std::chrono::milliseconds(3));

    // Advance time by 10ms so timers above all trigger in the same loop iteration.
    advanceLibeventTime(absl::Milliseconds(10));

    EXPECT_TRUE(timer1->enabled());
    EXPECT_TRUE(timer2->enabled());
    EXPECT_TRUE(timer3->enabled());
    EXPECT_TRUE(timer4->enabled());
  });
}

TEST_F(TimerImplTest, TimerChaining) {
  ReadyWatcher watcher1;
  Event::TimerPtr timer1 = dispatcher_->createTimer([&] { watcher1.ready(); });

  ReadyWatcher watcher2;
  Event::TimerPtr timer2 = dispatcher_->createTimer([&] {
    watcher2.ready();
    timer1->enableTimer(std::chrono::milliseconds(0));
  });

  ReadyWatcher watcher3;
  Event::TimerPtr timer3 = dispatcher_->createTimer([&] {
    watcher3.ready();
    timer2->enableTimer(std::chrono::milliseconds(0));
  });

  ReadyWatcher watcher4;
  Event::TimerPtr timer4 = dispatcher_->createTimer([&] {
    watcher4.ready();
    timer3->enableTimer(std::chrono::milliseconds(0));
  });

  timer4->enableTimer(std::chrono::milliseconds(0));

  EXPECT_FALSE(timer1->enabled());
  EXPECT_FALSE(timer2->enabled());
  EXPECT_FALSE(timer3->enabled());
  EXPECT_TRUE(timer4->enabled());
  InSequence s;
  EXPECT_CALL(prepare_watcher_, ready());
  EXPECT_CALL(watcher4, ready());
  EXPECT_CALL(prepare_watcher_, ready());
  EXPECT_CALL(watcher3, ready());
  EXPECT_CALL(prepare_watcher_, ready());
  EXPECT_CALL(watcher2, ready());
  EXPECT_CALL(prepare_watcher_, ready());
  EXPECT_CALL(watcher1, ready());
  dispatcher_->run(Dispatcher::RunType::NonBlock);

  EXPECT_FALSE(timer1->enabled());
  EXPECT_FALSE(timer2->enabled());
  EXPECT_FALSE(timer3->enabled());
  EXPECT_FALSE(timer4->enabled());
}

TEST_F(TimerImplTest, TimerChainDisable) {
  ReadyWatcher watcher;
  Event::TimerPtr timer1;
  Event::TimerPtr timer2;
  Event::TimerPtr timer3;

  auto timer_cb = [&] {
    watcher.ready();
    timer1->disableTimer();
    timer2->disableTimer();
    timer3->disableTimer();
  };

  timer1 = dispatcher_->createTimer(timer_cb);
  timer2 = dispatcher_->createTimer(timer_cb);
  timer3 = dispatcher_->createTimer(timer_cb);

  timer3->enableTimer(std::chrono::milliseconds(0));
  timer2->enableTimer(std::chrono::milliseconds(0));
  timer1->enableTimer(std::chrono::milliseconds(0));

  EXPECT_TRUE(timer1->enabled());
  EXPECT_TRUE(timer2->enabled());
  EXPECT_TRUE(timer3->enabled());
  InSequence s;
  // Only 1 call to watcher ready since the other 2 timers were disabled by the first timer.
  EXPECT_CALL(prepare_watcher_, ready());
  EXPECT_CALL(watcher, ready());
  dispatcher_->run(Dispatcher::RunType::NonBlock);
}

TEST_F(TimerImplTest, TimerChainDelete) {
  ReadyWatcher watcher;
  Event::TimerPtr timer1;
  Event::TimerPtr timer2;
  Event::TimerPtr timer3;

  auto timer_cb = [&] {
    watcher.ready();
    timer1.reset();
    timer2.reset();
    timer3.reset();
  };

  timer1 = dispatcher_->createTimer(timer_cb);
  timer2 = dispatcher_->createTimer(timer_cb);
  timer3 = dispatcher_->createTimer(timer_cb);

  timer3->enableTimer(std::chrono::milliseconds(0));
  timer2->enableTimer(std::chrono::milliseconds(0));
  timer1->enableTimer(std::chrono::milliseconds(0));

  EXPECT_TRUE(timer1->enabled());
  EXPECT_TRUE(timer2->enabled());
  EXPECT_TRUE(timer3->enabled());
  InSequence s;
  // Only 1 call to watcher ready since the other 2 timers were deleted by the first timer.
  EXPECT_CALL(prepare_watcher_, ready());
  EXPECT_CALL(watcher, ready());
  dispatcher_->run(Dispatcher::RunType::NonBlock);
}

class TimerImplTimingTest : public testing::Test {
public:
  std::chrono::nanoseconds getTimerTiming(Event::SimulatedTimeSystem& time_system,
                                          Dispatcher& dispatcher, Event::Timer& timer) {
    const auto start = time_system.monotonicTime();
    EXPECT_TRUE(timer.enabled());
    dispatcher.run(Dispatcher::RunType::NonBlock);
    while (timer.enabled()) {
      time_system.advanceTimeAndRun(std::chrono::microseconds(1), dispatcher,
                                    Dispatcher::RunType::NonBlock);
    }
    return time_system.monotonicTime() - start;
  }
};

// Test the timer with a series of timings and measure they fire accurately
// using simulated time. enableTimer() should be precise at the millisecond
// level, whereas enableHRTimer should be precise at the microsecond level.
// For good measure, also check that '0'/immediate does what it says on the tin.
TEST_F(TimerImplTimingTest, TheoreticalTimerTiming) {
  Event::SimulatedTimeSystem time_system;
  Api::ApiPtr api = Api::createApiForTest(time_system);
  DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  Event::TimerPtr timer = dispatcher->createTimer([&dispatcher] { dispatcher->exit(); });

  const uint64_t timings[] = {0, 10, 50, 1234};
  for (const uint64_t timing : timings) {
    std::chrono::milliseconds ms(timing);
    timer->enableTimer(ms);
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(
                  getTimerTiming(time_system, *dispatcher, *timer))
                  .count(),
              timing);

    std::chrono::microseconds us(timing);
    timer->enableHRTimer(us);
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::microseconds>(
                  getTimerTiming(time_system, *dispatcher, *timer))
                  .count(),
              timing);
  }
}

class TimerUtilsTest : public testing::Test {
public:
  template <typename Duration>
  void checkConversion(const Duration& duration, const uint64_t expected_secs,
                       const uint64_t expected_usecs) {
    timeval tv;
    TimerUtils::durationToTimeval(duration, tv);
    EXPECT_EQ(tv.tv_sec, expected_secs);
    EXPECT_EQ(tv.tv_usec, expected_usecs);
  }
};

TEST_F(TimerUtilsTest, TimerNegativeValueThrows) {
  timeval tv;
  const int negative_sample = -1;
  EXPECT_THROW_WITH_MESSAGE(
      TimerUtils::durationToTimeval(std::chrono::seconds(negative_sample), tv), EnvoyException,
      fmt::format("Negative duration passed to durationToTimeval(): {}", negative_sample));
}

TEST_F(TimerUtilsTest, TimerValueConversion) {
  // Check input is bounded.
  checkConversion(std::chrono::nanoseconds::duration::max(), INT32_MAX, 0);
  checkConversion(std::chrono::microseconds::duration::max(), INT32_MAX, 0);
  checkConversion(std::chrono::milliseconds::duration::max(), INT32_MAX, 0);
  checkConversion(std::chrono::seconds::duration::max(), INT32_MAX, 0);

  // Test the clipping boundary
  checkConversion(std::chrono::seconds(INT32_MAX) - std::chrono::seconds(1), INT32_MAX - 1, 0);
  checkConversion(std::chrono::seconds(INT32_MAX) - std::chrono::nanoseconds(1), INT32_MAX - 1,
                  999999);

  // Basic test with zero milliseconds.
  checkConversion(std::chrono::milliseconds(0), 0, 0);

  // 2050 milliseconds is 2 seconds and 50000 microseconds.
  checkConversion(std::chrono::milliseconds(2050), 2, 50000);

  // Some arbitrary tests for good measure.
  checkConversion(std::chrono::microseconds(233), 0, 233);

  // Some arbitrary tests for good measure.
  checkConversion(std::chrono::milliseconds(600014), 600, 14000);
}

TEST(DispatcherWithScaledTimerFactoryTest, CreatesScaledTimerManager) {
  Api::ApiPtr api = Api::createApiForTest();
  MockFunction<ScaledRangeTimerManagerFactory> scaled_timer_manager_factory;

  MockScaledRangeTimerManager* manager = new MockScaledRangeTimerManager();
  EXPECT_CALL(scaled_timer_manager_factory, Call)
      .WillOnce(Return(ByMove(ScaledRangeTimerManagerPtr(manager))));

  DispatcherPtr dispatcher =
      api->allocateDispatcher("test_thread", scaled_timer_manager_factory.AsStdFunction());
}

TEST(DispatcherWithScaledTimerFactoryTest, CreateScaledTimerWithMinimum) {
  Api::ApiPtr api = Api::createApiForTest();
  MockFunction<ScaledRangeTimerManagerFactory> scaled_timer_manager_factory;

  MockScaledRangeTimerManager* manager = new MockScaledRangeTimerManager();
  EXPECT_CALL(scaled_timer_manager_factory, Call)
      .WillOnce(Return(ByMove(ScaledRangeTimerManagerPtr(manager))));

  DispatcherPtr dispatcher =
      api->allocateDispatcher("test_thread", scaled_timer_manager_factory.AsStdFunction());

  EXPECT_CALL(*manager, createTimer_(ScaledTimerMinimum(ScaledMinimum(UnitFloat(0.8f))), _));
  dispatcher->createScaledTimer(ScaledTimerMinimum(ScaledMinimum(UnitFloat(0.8f))), []() {});
}

TEST(DispatcherWithScaledTimerFactoryTest, CreateScaledTimerWithTimerType) {
  Api::ApiPtr api = Api::createApiForTest();
  MockFunction<ScaledRangeTimerManagerFactory> scaled_timer_manager_factory;

  MockScaledRangeTimerManager* manager = new MockScaledRangeTimerManager();
  EXPECT_CALL(scaled_timer_manager_factory, Call)
      .WillOnce(Return(ByMove(ScaledRangeTimerManagerPtr(manager))));

  DispatcherPtr dispatcher =
      api->allocateDispatcher("test_thread", scaled_timer_manager_factory.AsStdFunction());

  EXPECT_CALL(*manager, createTypedTimer_(ScaledTimerType::UnscaledRealTimerForTest, _));
  dispatcher->createScaledTimer(ScaledTimerType::UnscaledRealTimerForTest, []() {});
}

class DispatcherWithWatchdogTest : public testing::Test {
protected:
  DispatcherWithWatchdogTest()
      : api_(Api::createApiForTest(time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")),
        os_sys_calls_(Api::OsSysCallsSingleton::get()) {
    dispatcher_->registerWatchdog(watchdog_, min_touch_interval_);
  }

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  DispatcherPtr dispatcher_;
  Api::OsSysCalls& os_sys_calls_;
  std::shared_ptr<Server::MockWatchDog> watchdog_ = std::make_shared<Server::MockWatchDog>();
  std::chrono::milliseconds min_touch_interval_ = std::chrono::seconds(10);
};

// The dispatcher creates a periodic touch timer for each registered watchdog.
TEST_F(DispatcherWithWatchdogTest, PeriodicTouchTimer) {
  // Advance by min_touch_interval_, verify that watchdog_ is touched.
  EXPECT_CALL(*watchdog_, touch());
  time_system_.advanceTimeAndRun(min_touch_interval_, *dispatcher_, Dispatcher::RunType::NonBlock);

  // Advance by min_touch_interval_ again, verify that watchdog_ is touched.
  EXPECT_CALL(*watchdog_, touch());
  time_system_.advanceTimeAndRun(min_touch_interval_, *dispatcher_, Dispatcher::RunType::NonBlock);
}

TEST_F(DispatcherWithWatchdogTest, TouchBeforeEachPostCallback) {
  ReadyWatcher watcher1;
  ReadyWatcher watcher2;
  ReadyWatcher watcher3;
  dispatcher_->post([&]() { watcher1.ready(); });
  dispatcher_->post([&]() { watcher2.ready(); });
  dispatcher_->post([&]() { watcher3.ready(); });

  InSequence s;
  EXPECT_CALL(*watchdog_, touch());
  EXPECT_CALL(watcher1, ready());
  EXPECT_CALL(*watchdog_, touch());
  EXPECT_CALL(watcher2, ready());
  EXPECT_CALL(*watchdog_, touch());
  EXPECT_CALL(watcher3, ready());
  dispatcher_->run(Dispatcher::RunType::NonBlock);
}

TEST_F(DispatcherWithWatchdogTest, TouchBeforeDeferredDelete) {
  ReadyWatcher watcher1;
  ReadyWatcher watcher2;
  ReadyWatcher watcher3;

  DeferredTaskUtil::deferredRun(*dispatcher_, [&watcher1]() -> void { watcher1.ready(); });
  DeferredTaskUtil::deferredRun(*dispatcher_, [&watcher2]() -> void { watcher2.ready(); });
  DeferredTaskUtil::deferredRun(*dispatcher_, [&watcher3]() -> void { watcher3.ready(); });

  InSequence s;
  EXPECT_CALL(*watchdog_, touch());
  EXPECT_CALL(watcher1, ready());
  EXPECT_CALL(watcher2, ready());
  EXPECT_CALL(watcher3, ready());
  dispatcher_->run(Dispatcher::RunType::NonBlock);
}

TEST_F(DispatcherWithWatchdogTest, TouchBeforeSchedulableCallback) {
  ReadyWatcher watcher;

  auto cb = dispatcher_->createSchedulableCallback([&]() { watcher.ready(); });
  cb->scheduleCallbackCurrentIteration();

  InSequence s;
  EXPECT_CALL(*watchdog_, touch());
  EXPECT_CALL(watcher, ready());
  dispatcher_->run(Dispatcher::RunType::NonBlock);
}

TEST_F(DispatcherWithWatchdogTest, TouchBeforeTimer) {
  ReadyWatcher watcher;

  auto timer = dispatcher_->createTimer([&]() { watcher.ready(); });
  timer->enableTimer(std::chrono::milliseconds(0));

  InSequence s;
  EXPECT_CALL(*watchdog_, touch());
  EXPECT_CALL(watcher, ready());
  dispatcher_->run(Dispatcher::RunType::NonBlock);
}

TEST_F(DispatcherWithWatchdogTest, TouchBeforeFdEvent) {
  os_fd_t fd = os_sys_calls_.socket(AF_INET6, SOCK_DGRAM, 0).return_value_;
  ASSERT_TRUE(SOCKET_VALID(fd));

  ReadyWatcher watcher;

  const FileTriggerType trigger = Event::PlatformDefaultTriggerType;
  Event::FileEventPtr file_event = dispatcher_->createFileEvent(
      fd, [&](uint32_t) -> void { watcher.ready(); }, trigger, FileReadyType::Read);
  file_event->activate(FileReadyType::Read);

  InSequence s;
  EXPECT_CALL(*watchdog_, touch()).Times(2);
  EXPECT_CALL(watcher, ready());
  dispatcher_->run(Dispatcher::RunType::NonBlock);
}

class DispatcherConnectionTest : public testing::Test {
protected:
  DispatcherConnectionTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  Api::ApiPtr api_;
  DispatcherPtr dispatcher_;
};

TEST_F(DispatcherConnectionTest, CreateTcpConnection) {
  for (auto ip_version : TestEnvironment::getIpVersionsForTest()) {
    SCOPED_TRACE(Network::Test::addressVersionAsString(ip_version));
    auto client_addr_port = Network::Utility::parseInternetAddressAndPort(
        fmt::format("{}:{}", Network::Test::getLoopbackAddressUrlString(ip_version), 10911));
    auto client_conn = dispatcher_->createClientConnection(
        client_addr_port, Network::Address::InstanceConstSharedPtr(),
        Network::Test::createRawBufferSocket(), nullptr, nullptr);
    EXPECT_NE(nullptr, client_conn);
    client_conn->close(Network::ConnectionCloseType::NoFlush);
  }
}

// If the internal connection factory is not linked, envoy will be dead when creating connection to
// internal address.
TEST_F(DispatcherConnectionTest, CreateEnvoyInternalConnectionWhenFactoryNotExist) {
  EXPECT_DEATH(
      dispatcher_->createClientConnection(
          std::make_shared<Network::Address::EnvoyInternalInstance>("listener_internal_address"),
          Network::Address::InstanceConstSharedPtr(), Network::Test::createRawBufferSocket(),
          nullptr, nullptr),
      "");
}

} // namespace
} // namespace Event
} // namespace Envoy
