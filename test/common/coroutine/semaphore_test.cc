#include <memory>
#include <string>
#include <vector>

#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/semaphore.h"
#include "source/common/coroutine/task.h"

#include "test/common/coroutine/manual_executor.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Coroutine {
namespace {

using ::Envoy::StatusHelpers::HasStatusCode;

class SemaphoreTest : public testing::Test {
public:
  SemaphoreTest() : executor_(std::make_shared<ManualExecutor>()) {}

  void drain() { executor_->drain(); }

  void launchTaskOk(Task<absl::Status> task) {
    handles_.push_back(
        launch(std::move(task), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  }

  std::shared_ptr<ManualExecutor> executor_;
  std::vector<DetachedHandle> handles_;
};

TEST_F(SemaphoreTest, UnboundedSemaphoreAcquireRelease) {
  auto sem = std::make_shared<Semaphore>();
  EXPECT_FALSE(sem->maxPermits().has_value());
  EXPECT_EQ(sem->currentPermits(), 0);

  auto res1 = sem->tryAcquire(5);
  ASSERT_TRUE(res1.has_value());
  EXPECT_EQ(res1->permits(), 5);
  EXPECT_EQ(sem->currentPermits(), 5);

  auto res2 = sem->tryAcquire(10);
  ASSERT_TRUE(res2.has_value());
  EXPECT_EQ(res2->permits(), 10);
  EXPECT_EQ(sem->currentPermits(), 15);

  res1->release();
  EXPECT_EQ(res1->permits(), 0);
  EXPECT_EQ(sem->currentPermits(), 10);

  res2.reset();
  EXPECT_EQ(sem->currentPermits(), 0);
}

TEST_F(SemaphoreTest, BoundedSemaphoreTryAcquire) {
  auto sem = std::make_shared<Semaphore>(10);
  EXPECT_EQ(sem->maxPermits().value(), 10);
  EXPECT_EQ(sem->currentPermits(), 0);

  auto res1 = sem->tryAcquire(6);
  ASSERT_TRUE(res1.has_value());
  EXPECT_EQ(sem->currentPermits(), 6);

  // Exceeds remaining capacity (4 available, requesting 5)
  auto res2 = sem->tryAcquire(5);
  EXPECT_FALSE(res2.has_value());
  EXPECT_EQ(sem->currentPermits(), 6);

  // Fits in remaining capacity
  auto res3 = sem->tryAcquire(4);
  ASSERT_TRUE(res3.has_value());
  EXPECT_EQ(sem->currentPermits(), 10);

  res1.reset();
  EXPECT_EQ(sem->currentPermits(), 4);
  drain();

  // Now 5 fits
  auto res4 = sem->tryAcquire(5);
  ASSERT_TRUE(res4.has_value());
  EXPECT_EQ(sem->currentPermits(), 9);
}

TEST_F(SemaphoreTest, AsyncAcquireAndFifoOrder) {
  auto sem = std::make_shared<Semaphore>(10);
  auto hold = sem->tryAcquire(10);
  ASSERT_TRUE(hold.has_value());

  std::vector<int> acquired_order;
  std::vector<SemaphoreReservation> held;

  auto acquireTask = [](SemaphorePtr s, uint64_t permits, int id, std::vector<int>* order,
                        std::vector<SemaphoreReservation>* held_res) -> Task<absl::Status> {
    ASSIGN_OR_CO_RETURN(auto res, co_await s->acquire(permits));
    order->push_back(id);
    held_res->push_back(std::move(res));
    co_return absl::OkStatus();
  };

  launchTaskOk(acquireTask(sem, 5, 1, &acquired_order, &held));
  launchTaskOk(acquireTask(sem, 5, 2, &acquired_order, &held));
  launchTaskOk(acquireTask(sem, 5, 3, &acquired_order, &held));
  drain();
  EXPECT_TRUE(acquired_order.empty());

  // Release initial reservation; waiter 1 and waiter 2 should be satisfied up to capacity 10.
  hold.reset();
  drain();
  EXPECT_THAT(acquired_order, testing::ElementsAre(1, 2));

  // Release waiter 1's permits; waiter 3 should now be satisfied.
  held[0].release();
  drain();
  EXPECT_THAT(acquired_order, testing::ElementsAre(1, 2, 3));
}

TEST_F(SemaphoreTest, FifoHeadOfLineBlocking) {
  auto sem = std::make_shared<Semaphore>(10);
  auto hold = sem->tryAcquire(10);
  ASSERT_TRUE(hold.has_value());

  std::vector<int> acquired_order;
  std::vector<SemaphoreReservation> held;

  auto acquireTask = [](SemaphorePtr s, uint64_t permits, int id, std::vector<int>* order,
                        std::vector<SemaphoreReservation>* held_res) -> Task<absl::Status> {
    ASSIGN_OR_CO_RETURN(auto res, co_await s->acquire(permits));
    order->push_back(id);
    held_res->push_back(std::move(res));
    co_return absl::OkStatus();
  };

  // Waiter 1 requests 8 permits. Waiter 2 requests 2 permits.
  launchTaskOk(acquireTask(sem, 8, 1, &acquired_order, &held));
  launchTaskOk(acquireTask(sem, 2, 2, &acquired_order, &held));
  drain();
  EXPECT_TRUE(acquired_order.empty());

  // Release hold (10 permits); both waiter 1 (8 permits) and waiter 2 (2 permits) fit.
  hold.reset();
  drain();
  EXPECT_THAT(acquired_order, testing::ElementsAre(1, 2));
}

TEST_F(SemaphoreTest, CancellationUnblocksNextWaiters) {
  auto sem = std::make_shared<Semaphore>(10);
  auto hold = sem->tryAcquire(10);
  ASSERT_TRUE(hold.has_value());

  std::vector<int> acquired_order;
  std::vector<SemaphoreReservation> held;

  auto acquireTask = [](SemaphorePtr s, uint64_t permits, int id, std::vector<int>* order,
                        std::vector<SemaphoreReservation>* held_res) -> Task<absl::Status> {
    ASSIGN_OR_CO_RETURN(auto res, co_await s->acquire(permits));
    order->push_back(id);
    held_res->push_back(std::move(res));
    co_return absl::OkStatus();
  };

  DetachedHandle h1 =
      launch(acquireTask(sem, 8, 1, &acquired_order, &held), executor_, [](absl::Status status) {
        EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kCancelled));
      });
  DetachedHandle h2 = launch(acquireTask(sem, 4, 2, &acquired_order, &held), executor_,
                             [](absl::Status status) { EXPECT_OK(status); });
  drain();
  EXPECT_TRUE(acquired_order.empty());

  // Cancel waiter 1 (head of line).
  h1.cancel();
  drain();

  // Release hold (10 permits); waiter 2 is now the head and acquires 4 permits.
  hold.reset();
  drain();
  EXPECT_THAT(acquired_order, testing::ElementsAre(2));
}

TEST_F(SemaphoreTest, DestructionFailsWaiters) {
  auto sem = std::make_shared<Semaphore>(10);
  auto hold = sem->tryAcquire(10);
  ASSERT_TRUE(hold.has_value());

  bool waiter_failed = false;
  auto acquireTask = [](Semaphore& s, bool* failed) -> Task<absl::Status> {
    auto res = co_await s.acquire(5);
    if (!res.ok() && res.status().code() == absl::StatusCode::kFailedPrecondition) {
      *failed = true;
    }
    co_return absl::OkStatus();
  };

  launchTaskOk(acquireTask(*sem, &waiter_failed));
  drain();
  EXPECT_FALSE(waiter_failed);

  // Destroy semaphore while waiter is pending.
  sem.reset();
  drain();
  EXPECT_TRUE(waiter_failed);
}

TEST_F(SemaphoreTest, ReservationOutlivesSemaphoreSafely) {
  SemaphoreReservation res;
  {
    auto sem = std::make_shared<Semaphore>(10);
    auto opt = sem->tryAcquire(4);
    ASSERT_TRUE(opt.has_value());
    res = std::move(*opt);
    EXPECT_EQ(res.permits(), 4);
    // Semaphore destroyed here.
  }
  // Reservation dropped after semaphore destruction must not crash or leak.
  res.release();
  EXPECT_EQ(res.permits(), 0);
}

TEST_F(SemaphoreTest, ReservationHasPermits) {
  auto sem = std::make_shared<Semaphore>(10);
  SemaphoreReservation empty_res;
  EXPECT_FALSE(empty_res.hasPermits());

  auto res = sem->tryAcquire(5);
  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(res->hasPermits());
  EXPECT_EQ(res->permits(), 5);

  res->release();
  EXPECT_FALSE(res->hasPermits());

  // Test when semaphore is destroyed
  auto res2 = sem->tryAcquire(3);
  ASSERT_TRUE(res2.has_value());
  EXPECT_TRUE(res2->hasPermits());
  sem.reset();
  EXPECT_FALSE(res2->hasPermits());
}

TEST_F(SemaphoreTest, ReservationMoveAssignmentOverwritesActiveReservation) {
  auto sem = std::make_shared<Semaphore>(10);
  auto res1 = sem->tryAcquire(4);
  auto res2 = sem->tryAcquire(3);
  ASSERT_TRUE(res1.has_value());
  ASSERT_TRUE(res2.has_value());
  EXPECT_EQ(sem->currentPermits(), 7);

  // Overwriting active reservation res1 triggers ENVOY_BUG
  EXPECT_ENVOY_BUG(*res1 = std::move(*res2),
                   "SemaphoreReservation should not overwrite an active reservation");
}

TEST_F(SemaphoreTest, UnboundedAsyncAcquire) {
  auto sem = std::make_shared<Semaphore>(); // unbounded
  auto hold = sem->tryAcquire(100);
  ASSERT_TRUE(hold.has_value());

  bool acquired = false;
  launchTaskOk([&]() -> Task<absl::Status> {
    ASSIGN_OR_CO_RETURN(auto res, co_await sem->acquire(50));
    acquired = true;
    co_return absl::OkStatus();
  }());
  drain();
  EXPECT_TRUE(acquired);
}

TEST_F(SemaphoreTest, IntegerOverflowPermits) {
  auto sem = std::make_shared<Semaphore>(10);
  auto hold = sem->tryAcquire(5);
  ASSERT_TRUE(hold.has_value());

  // Requesting huge permits causing integer overflow in current_permits + additional_permits
  uint64_t huge_permits = std::numeric_limits<uint64_t>::max();
  EXPECT_FALSE(sem->tryAcquire(huge_permits).has_value());
}

TEST_F(SemaphoreTest, ReleaseZeroPermitsIsNoOp) {
  auto sem = std::make_shared<Semaphore>(10);
  sem->release(0); // no-op
  EXPECT_EQ(sem->currentPermits(), 0);

  auto res = sem->tryAcquire(5);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(sem->currentPermits(), 5);

  sem->release(0); // no-op when permits held
  EXPECT_EQ(sem->currentPermits(), 5);

  res->release();
  EXPECT_EQ(sem->currentPermits(), 0);
}

TEST_F(SemaphoreTest, DestructionWhileProcessWaitersScheduled) {
  auto sem = std::make_shared<Semaphore>(10);
  auto hold = sem->tryAcquire(10);
  ASSERT_TRUE(hold.has_value());

  bool waiter_failed = false;
  launchTaskOk([&]() -> Task<absl::Status> {
    auto res = co_await sem->acquire(5);
    if (!res.ok()) {
      waiter_failed = true;
    }
    co_return absl::OkStatus();
  }());
  drain(); // Waiter is now queued in waiters_

  // Release permits: this schedules runScheduledProcessWaiters on executor_
  hold->release();
  // Destroy semaphore while scheduled task is in executor queue:
  sem.reset();
  // Now drain: scheduled task runs, observes !*alive, and completes without crash
  drain();
  EXPECT_TRUE(waiter_failed);
}

TEST_F(SemaphoreTest, AllWaitersCancelledBeforeScheduleOrProcess) {
  auto sem = std::make_shared<Semaphore>(10);
  auto hold = sem->tryAcquire(10);
  ASSERT_TRUE(hold.has_value());

  DetachedHandle h1 = launch(
      [&]() -> Task<absl::Status> {
        auto res = co_await sem->acquire(5);
        co_return absl::OkStatus();
      }(),
      executor_, [](absl::Status) {});

  DetachedHandle h2 = launch(
      [&]() -> Task<absl::Status> {
        auto res = co_await sem->acquire(5);
        co_return absl::OkStatus();
      }(),
      executor_, [](absl::Status) {});

  drain(); // Waiters queued

  // Cancel both waiters
  h1.cancel();
  h2.cancel();

  // Releasing hold will trigger scheduleProcessWaiters which pops cancelled waiters and sees empty
  hold->release();
  drain();
  EXPECT_EQ(sem->currentPermits(), 0);
}

TEST_F(SemaphoreTest, TrailingCancelledWaiterDuringProcessWaiters) {
  auto sem = std::make_shared<Semaphore>(10);
  auto hold = sem->tryAcquire(10);
  ASSERT_TRUE(hold.has_value());

  SemaphoreReservation w1_res;
  launchTaskOk([&]() -> Task<absl::Status> {
    ASSIGN_OR_CO_RETURN(w1_res, co_await sem->acquire(5));
    co_return absl::OkStatus();
  }());

  DetachedHandle h2 = launch(
      [&]() -> Task<absl::Status> {
        auto res = co_await sem->acquire(5);
        co_return absl::OkStatus();
      }(),
      executor_, [](absl::Status) {});

  drain(); // w1 and w2 queued

  // Cancel w2 (trailing waiter)
  h2.cancel();

  // Release hold: processWaiters will satisfy w1 in iteration 1, then pop cancelled w2 and
  // break on empty
  hold->release();
  drain();
  EXPECT_TRUE(w1_res.hasPermits());
  EXPECT_EQ(sem->currentPermits(), 5);
}

} // namespace
} // namespace Coroutine
} // namespace Envoy
