#include <condition_variable>
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
  DispatcherImplTest() : dispatcher_(std::make_unique<DispatcherImpl>()), work_finished_(false) {
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
  std::condition_variable cv_;

  bool work_finished_;
  TimerPtr keepalive_timer_;
};

TEST_F(DispatcherImplTest, Post) {
  dispatcher_->post([this]() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      work_finished_ = true;
    }
    cv_.notify_one();
  });

  std::unique_lock<std::mutex> lock(mu_);

  cv_.wait(lock, [this]() { return work_finished_; });
}

TEST_F(DispatcherImplTest, Timer) {
  TimerPtr timer;
  dispatcher_->post([this, &timer]() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      timer = dispatcher_->createTimer([this]() {
        {
          std::lock_guard<std::mutex> lock(mu_);
          work_finished_ = true;
        }
        cv_.notify_one();
      });
    }
    cv_.notify_one();
  });

  std::unique_lock<std::mutex> lock(mu_);
  cv_.wait(lock, [&timer]() { return timer != nullptr; });
  timer->enableTimer(std::chrono::milliseconds(50));

  cv_.wait(lock, [this]() { return work_finished_; });
}

} // namespace Event
} // namespace Envoy
