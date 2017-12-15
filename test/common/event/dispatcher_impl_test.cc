#include <functional>
#include <mutex>

#include "common/common/thread.h"
#include "common/event/dispatcher_impl.h"

#include "test/mocks/common.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;

namespace Envoy {
namespace Event {

class TestDeferredDeletable : public DeferredDeletable {
public:
  TestDeferredDeletable(std::function<void()> on_destroy) : on_destroy_(on_destroy) {}
  ~TestDeferredDeletable() { on_destroy_(); }

private:
  std::function<void()> on_destroy_;
};

TEST(DeferredDeleteTest, DeferredDelete) {
  InSequence s;
  DispatcherImpl dispatcher;
  ReadyWatcher watcher1;

  dispatcher.deferredDelete(
      DeferredDeletablePtr{new TestDeferredDeletable([&]() -> void { watcher1.ready(); })});

  // The first one will get deleted inline.
  EXPECT_CALL(watcher1, ready());
  dispatcher.clearDeferredDeleteList();

  // This one does a nested deferred delete. We should need two clear calls to actually get
  // rid of it with the vector swapping. We also test that inline clear() call does nothing.
  ReadyWatcher watcher2;
  ReadyWatcher watcher3;
  dispatcher.deferredDelete(DeferredDeletablePtr{new TestDeferredDeletable([&]() -> void {
    watcher2.ready();
    dispatcher.deferredDelete(
        DeferredDeletablePtr{new TestDeferredDeletable([&]() -> void { watcher3.ready(); })});
    dispatcher.clearDeferredDeleteList();
  })});

  EXPECT_CALL(watcher2, ready());
  dispatcher.clearDeferredDeleteList();

  EXPECT_CALL(watcher3, ready());
  dispatcher.clearDeferredDeleteList();
}

class DispatcherImplTest : public ::testing::Test {
protected:
  DispatcherImplTest() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      work_id_ = 0;
    }
    dispatcher_ = std::make_unique<DispatcherImpl>();
    dispatcher_thread_ = std::make_unique<Thread::Thread>([this]() {
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

  std::unique_ptr<Thread::Thread> dispatcher_thread_;
  DispatcherPtr dispatcher_;
  std::mutex mu_;
  int work_id_;
  TimerPtr keepalive_timer_;
};

TEST_F(DispatcherImplTest, Post) {
  dispatcher_->post([this]() {
    std::lock_guard<std::mutex> lock(mu_);
    work_id_ = 1;
  });

  bool reached_value = false;
  while (!reached_value) {
    std::lock_guard<std::mutex> lock(mu_);
    reached_value = work_id_ == 1;
  }
}

TEST_F(DispatcherImplTest, Timer) {
  std::unique_ptr<Timer> timer;
  dispatcher_->post([this, &timer]() {

    std::lock_guard<std::mutex> lock(mu_);
    timer = dispatcher_->createTimer([this]() {
      std::lock_guard<std::mutex> lock(mu_);
      work_id_ = 1;
    });

  });

  bool reached_value = false;
  while (!reached_value) {
    std::lock_guard<std::mutex> lock(mu_);
    if (timer) {
      timer->enableTimer(std::chrono::milliseconds(50));
      reached_value = true;
    }
  }

  reached_value = false;
  while (!reached_value) {
    std::lock_guard<std::mutex> lock(mu_);
    reached_value = work_id_ == 1;
  }

  {
    std::lock_guard<std::mutex> lock(mu_);
    EXPECT_TRUE(timer);
  }
}

} // namespace Event
} // namespace Envoy
