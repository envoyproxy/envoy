#include <memory>
#include <vector>

#include "source/common/coroutine/async_event.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/task.h"

#include "test/common/coroutine/manual_executor.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Coroutine {
namespace {

class AsyncEventTest : public testing::Test {
protected:
  void drain() { executor_->drain(); }

  static Task<absl::Status> waitTask(AsyncEvent& event, bool* done = nullptr) {
    CO_RETURN_IF_ERROR(co_await event.wait());
    if (done != nullptr) {
      *done = true;
    }
    co_return absl::OkStatus();
  }

  static Task<absl::Status> waitFrontTask(AsyncEvent& event, bool* done = nullptr) {
    CO_RETURN_IF_ERROR(co_await event.waitFront());
    if (done != nullptr) {
      *done = true;
    }
    co_return absl::OkStatus();
  }

  static Task<absl::Status> waitOrderTask(AsyncEvent& event, std::vector<int>* order, int id,
                                          bool front = false) {
    CO_RETURN_IF_ERROR(co_await (front ? event.waitFront() : event.wait()));
    if (order != nullptr) {
      order->push_back(id);
    }
    co_return absl::OkStatus();
  }

  DetachedHandle launchWait(AsyncEvent& event, bool& done) {
    return launch(waitTask(event, &done), executor_, [](absl::Status) {});
  }

  DetachedHandle launchWaitFront(AsyncEvent& event, bool& done) {
    return launch(waitFrontTask(event, &done), executor_, [](absl::Status) {});
  }

  DetachedHandle launchWaitOrder(AsyncEvent& event, std::vector<int>& order, int id,
                                 bool front = false) {
    return launch(waitOrderTask(event, &order, id, front), executor_, [](absl::Status) {});
  }

  std::shared_ptr<ManualExecutor> executor_{std::make_shared<ManualExecutor>()};
  std::vector<DetachedHandle> handles_;
};

TEST_F(AsyncEventTest, NotifyOneWakesFirstWaiter) {
  AsyncEvent event;
  EXPECT_FALSE(event.hasWaiters());

  bool w1 = false;
  bool w2 = false;
  handles_.push_back(launchWait(event, w1));
  handles_.push_back(launchWait(event, w2));
  drain();

  EXPECT_TRUE(event.hasWaiters());
  EXPECT_FALSE(w1);
  EXPECT_FALSE(w2);

  event.notifyOne();
  drain();
  EXPECT_TRUE(w1);
  EXPECT_FALSE(w2);
  EXPECT_TRUE(event.hasWaiters());

  event.notifyOne();
  drain();
  EXPECT_TRUE(w2);
  EXPECT_FALSE(event.hasWaiters());
}

TEST_F(AsyncEventTest, NotifyAllWakesAllWaiters) {
  AsyncEvent event;
  bool w1 = false;
  bool w2 = false;
  bool w3 = false;
  handles_.push_back(launchWait(event, w1));
  handles_.push_back(launchWait(event, w2));
  handles_.push_back(launchWait(event, w3));
  drain();

  EXPECT_TRUE(event.hasWaiters());
  event.notifyAll();
  drain();

  EXPECT_TRUE(w1 && w2 && w3);
  EXPECT_FALSE(event.hasWaiters());
}

TEST_F(AsyncEventTest, WaitFrontPrioritizesWaiter) {
  AsyncEvent event;
  std::vector<int> order;
  handles_.push_back(launchWaitOrder(event, order, 1, /*front=*/false));
  drain();
  handles_.push_back(launchWaitOrder(event, order, 2, /*front=*/true));
  drain();

  event.notifyOne();
  drain();
  event.notifyOne();
  drain();

  EXPECT_THAT(order, testing::ElementsAre(2, 1));
}

TEST_F(AsyncEventTest, CancellationUnregistersWaiter) {
  AsyncEvent event;
  bool w1 = false;
  bool w2 = false;
  DetachedHandle h1 = launchWait(event, w1);
  handles_.push_back(launchWait(event, w2));
  drain();

  h1.cancel();
  drain();

  event.notifyOne();
  drain();
  EXPECT_FALSE(w1);
  EXPECT_TRUE(w2);
  EXPECT_FALSE(event.hasWaiters());
}

TEST_F(AsyncEventTest, ReentrantCancellationDuringNotifyAll) {
  AsyncEvent event;
  std::optional<DetachedHandle> h2;
  bool w1 = false;
  bool w2 = false;

  auto task1 = [&]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await event.wait());
    w1 = true;
    if (h2.has_value()) {
      h2->cancel();
    }
    co_return absl::OkStatus();
  };

  handles_.push_back(launch(task1(), executor_, [](absl::Status) {}));
  h2 = launchWait(event, w2);
  drain();

  event.notifyAll();
  drain();

  EXPECT_TRUE(w1);
  EXPECT_FALSE(w2);
  EXPECT_FALSE(event.hasWaiters());
}

TEST_F(AsyncEventTest, ReentrantWaitDuringNotifyAllDoesNotWakeInSamePass) {
  AsyncEvent event;
  bool w1 = false;
  bool new_waiter = false;

  auto task1 = [&]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await event.wait());
    w1 = true;
    handles_.push_back(launchWait(event, new_waiter));
    drain();
    co_return absl::OkStatus();
  };

  handles_.push_back(launch(task1(), executor_, [](absl::Status) {}));
  drain();

  event.notifyAll();
  drain();
  EXPECT_TRUE(w1);
  EXPECT_FALSE(new_waiter);
  EXPECT_TRUE(event.hasWaiters());

  event.notifyAll();
  drain();
  EXPECT_TRUE(new_waiter);
  EXPECT_FALSE(event.hasWaiters());
}

TEST_F(AsyncEventTest, ReentrantCancelAndAddDuringNotifyAll) {
  AsyncEvent event;
  std::optional<DetachedHandle> h_b;
  bool a = false;
  bool b = false;
  bool c = false;
  bool d = false;

  auto task_a = [&]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await event.wait());
    a = true;
    if (h_b.has_value()) {
      h_b->cancel();
    }
    handles_.push_back(launchWait(event, d));
    drain();
    co_return absl::OkStatus();
  };

  handles_.push_back(launch(task_a(), executor_, [](absl::Status) {}));
  h_b = launchWait(event, b);
  handles_.push_back(launchWait(event, c));
  drain();

  event.notifyAll();
  drain();

  EXPECT_TRUE(a);
  EXPECT_FALSE(b);
  EXPECT_TRUE(c);
  EXPECT_FALSE(d);

  event.notifyAll();
  drain();
  EXPECT_TRUE(d);
  EXPECT_FALSE(event.hasWaiters());
}

TEST_F(AsyncEventTest, ReentrantWaitFrontDuringNotifyAllDoesNotWakeInSamePass) {
  AsyncEvent event;
  bool w1 = false;
  bool front_waiter = false;

  auto task1 = [&]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await event.wait());
    w1 = true;
    handles_.push_back(launchWaitFront(event, front_waiter));
    drain();
    co_return absl::OkStatus();
  };

  handles_.push_back(launch(task1(), executor_, [](absl::Status) {}));
  drain();

  event.notifyAll();
  drain();
  EXPECT_TRUE(w1);
  EXPECT_FALSE(front_waiter);

  event.notifyAll();
  drain();
  EXPECT_TRUE(front_waiter);
  EXPECT_FALSE(event.hasWaiters());
}

} // namespace
} // namespace Coroutine
} // namespace Envoy
