#include <functional>

#include "envoy/thread/thread.h"

#include "common/api/api_impl.h"
#include "common/common/lock_guard.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/timer_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::NiceMock;

namespace Envoy {
namespace Event {
namespace {

class TestDeferredDeletable : public DeferredDeletable {
public:
  TestDeferredDeletable(std::function<void()> on_destroy) : on_destroy_(on_destroy) {}
  ~TestDeferredDeletable() override { on_destroy_(); }

private:
  std::function<void()> on_destroy_;
};

TEST(DeferredDeleteTest, DeferredDelete) {
  InSequence s;
  Api::ApiPtr api = Api::createApiForTest();
  DispatcherPtr dispatcher(api->allocateDispatcher());
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

class DispatcherImplTest : public testing::Test {
protected:
  DispatcherImplTest() : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher()) {
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
      work_finished_ = true;
    }
    cv_.notifyOne();
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

TEST_F(DispatcherImplTest, Timer) {
  TimerPtr timer;
  dispatcher_->post([this, &timer]() {
    {
      Thread::LockGuard lock(mu_);
      timer = dispatcher_->createTimer([this]() {
        {
          Thread::LockGuard lock(mu_);
          work_finished_ = true;
        }
        cv_.notifyOne();
      });
      EXPECT_FALSE(timer->enabled());
    }
    cv_.notifyOne();
  });

  Thread::LockGuard lock(mu_);
  while (timer == nullptr) {
    cv_.wait(mu_);
  }
  timer->enableTimer(std::chrono::milliseconds(50));

  while (!work_finished_) {
    cv_.wait(mu_);
  }
}

TEST_F(DispatcherImplTest, TimerWithScope) {
  TimerPtr timer;
  MockScopedTrackedObject scope;
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
          static_cast<DispatcherImpl*>(dispatcher_.get())->onFatalError();
          work_finished_ = true;
        }
        cv_.notifyOne();
      });
      EXPECT_FALSE(timer->enabled());
    }
    cv_.notifyOne();
  });

  Thread::LockGuard lock(mu_);
  while (timer == nullptr) {
    cv_.wait(mu_);
  }
  timer->enableTimer(std::chrono::milliseconds(50), &scope);

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

class NotStartedDispatcherImplTest : public testing::Test {
protected:
  NotStartedDispatcherImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher()) {}

  Api::ApiPtr api_;
  DispatcherPtr dispatcher_;
};

TEST_F(NotStartedDispatcherImplTest, IsThreadSafe) {
  // Thread safe because the dispatcher has not started.
  // Therefore, no thread id has been assigned.
  EXPECT_TRUE(dispatcher_->isThreadSafe());
}

TEST(TimerImplTest, TimerEnabledDisabled) {
  Api::ApiPtr api = Api::createApiForTest();
  DispatcherPtr dispatcher(api->allocateDispatcher());
  Event::TimerPtr timer = dispatcher->createTimer([] {});
  EXPECT_FALSE(timer->enabled());
  timer->enableTimer(std::chrono::milliseconds(0));
  EXPECT_TRUE(timer->enabled());
  dispatcher->run(Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(timer->enabled());
}

TEST(TimerImplTest, TimerValueConversion) {
  timeval tv;
  std::chrono::milliseconds msecs;

  // Basic test with zero milliseconds.
  msecs = std::chrono::milliseconds(0);
  TimerUtils::millisecondsToTimeval(msecs, tv);
  EXPECT_EQ(tv.tv_sec, 0);
  EXPECT_EQ(tv.tv_usec, 0);

  // 2050 milliseconds is 2 seconds and 50000 microseconds.
  msecs = std::chrono::milliseconds(2050);
  TimerUtils::millisecondsToTimeval(msecs, tv);
  EXPECT_EQ(tv.tv_sec, 2);
  EXPECT_EQ(tv.tv_usec, 50000);

  // Check maximum value conversion.
  msecs = std::chrono::milliseconds::duration::max();
  TimerUtils::millisecondsToTimeval(msecs, tv);
  EXPECT_EQ(tv.tv_sec, msecs.count() / 1000);
  EXPECT_EQ(tv.tv_usec, (msecs.count() % tv.tv_sec) * 1000);
}

} // namespace
} // namespace Event
} // namespace Envoy
