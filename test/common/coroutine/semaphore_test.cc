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

TEST_F(SemaphoreTest, ReservationLifecycle) {
  // Empty reservation
  SemaphoreReservation empty_res;
  EXPECT_FALSE(empty_res.hasPermits());

  // Active reservation acquires permits and hasPermits() is true
  auto sem = std::make_shared<Semaphore>(10);
  auto res1 = sem->tryAcquire(5);
  ASSERT_TRUE(res1.has_value());
  EXPECT_TRUE(res1->hasPermits());
  EXPECT_EQ(res1->permits(), 5);

  // Explicit release clears permits
  res1->release();
  EXPECT_FALSE(res1->hasPermits());
  EXPECT_EQ(sem->currentPermits(), 0);

  // Reservation outliving semaphore releases safely without crash or leak
  SemaphoreReservation outliving_res;
  {
    auto scoped_sem = std::make_shared<Semaphore>(10);
    auto opt = scoped_sem->tryAcquire(4);
    ASSERT_TRUE(opt.has_value());
    outliving_res = std::move(*opt);
    EXPECT_EQ(outliving_res.permits(), 4);
    // `scoped_sem` is destroyed here.
  }
  EXPECT_FALSE(outliving_res.hasPermits()); // `sem` is destroyed
  outliving_res.release();                  // Dropping after destruction is safe
  EXPECT_EQ(outliving_res.permits(), 0);

  // Move-assignment over an active reservation triggers ENVOY_BUG
  auto r1 = sem->tryAcquire(4);
  auto r2 = sem->tryAcquire(3);
  ASSERT_TRUE(r1.has_value());
  ASSERT_TRUE(r2.has_value());
  EXPECT_EQ(sem->currentPermits(), 7);
  EXPECT_ENVOY_BUG(*r1 = std::move(*r2),
                   "SemaphoreReservation should not overwrite an active reservation");
}

TEST_F(SemaphoreTest, UnboundedAsyncAcquire) {
  auto sem = std::make_shared<Semaphore>(); // unbounded
  auto hold = sem->tryAcquire(100);
  ASSERT_TRUE(hold.has_value());

  auto acquireTask = [](SemaphorePtr s, bool* acquired) -> Task<absl::Status> {
    ASSIGN_OR_CO_RETURN(auto res, co_await s->acquire(50));
    *acquired = true;
    co_return absl::OkStatus();
  };

  bool acquired = false;
  launchTaskOk(acquireTask(sem, &acquired));
  drain();
  EXPECT_TRUE(acquired);
}

TEST_F(SemaphoreTest, PermitBoundaryConditions) {
  auto sem = std::make_shared<Semaphore>(10);

  // Zero-permit acquire and release are no-ops
  auto zero_res1 = sem->tryAcquire(0);
  ASSERT_TRUE(zero_res1.has_value());
  EXPECT_FALSE(zero_res1->hasPermits());
  zero_res1->release();
  EXPECT_EQ(sem->currentPermits(), 0);

  auto res = sem->tryAcquire(5);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(sem->currentPermits(), 5);

  auto zero_res2 = sem->tryAcquire(0);
  ASSERT_TRUE(zero_res2.has_value());
  EXPECT_FALSE(zero_res2->hasPermits());
  zero_res2->release();
  EXPECT_EQ(sem->currentPermits(), 5);

  // When partially in use, oversized/overflow acquire requests are rejected
  uint64_t huge_permits = std::numeric_limits<uint64_t>::max();
  EXPECT_FALSE(sem->tryAcquire(huge_permits).has_value());

  res->release();
  EXPECT_EQ(sem->currentPermits(), 0);
}

TEST_F(SemaphoreTest, ReleaseWhenAllWaitersCancelled) {
  auto sem = std::make_shared<Semaphore>(1);
  auto hold = sem->tryAcquire(1);
  ASSERT_TRUE(hold.has_value());

  auto acquireTask = [](Semaphore& s) -> Task<absl::Status> {
    auto res = co_await s.acquire(1);
    co_return absl::OkStatus();
  };

  auto handle = launch(acquireTask(*sem), executor_, [](absl::Status) {});
  drain();

  // Cancel the pending waiter
  handle.cancel();

  // Release capacity: scheduleProcessWaiters pops the cancelled waiter and sees empty list
  hold->release();
  drain();
  EXPECT_EQ(sem->currentPermits(), 0);
}

TEST_F(SemaphoreTest, DestructionWhileProcessWaitersScheduled) {
  auto sem = std::make_shared<Semaphore>(10);
  auto hold = sem->tryAcquire(10);
  ASSERT_TRUE(hold.has_value());

  auto acquireTask = [](Semaphore& s, bool* failed) -> Task<absl::Status> {
    auto res = co_await s.acquire(5);
    if (!res.ok()) {
      *failed = true;
    }
    co_return absl::OkStatus();
  };

  bool waiter_failed = false;
  launchTaskOk(acquireTask(*sem, &waiter_failed));
  drain(); // Waiter is now queued in waiters_

  // Release permits: this schedules runScheduledProcessWaiters on executor_
  hold->release();
  // Destroy semaphore while scheduled task is in executor queue:
  sem.reset();
  // Now drain: scheduled task runs, observes !*alive, and completes without crash
  drain();
  EXPECT_TRUE(waiter_failed);
}

TEST_F(SemaphoreTest, CancellationDuringScheduledProcessing) {
  auto sem = std::make_shared<Semaphore>(10);
  auto hold = sem->tryAcquire(10);
  ASSERT_TRUE(hold.has_value());

  auto acquireTask = [](SemaphorePtr s,
                        SemaphoreReservation* out_res = nullptr) -> Task<absl::Status> {
    ASSIGN_OR_CO_RETURN(auto res, co_await s->acquire(5));
    if (out_res != nullptr) {
      *out_res = std::move(res);
    }
    co_return absl::OkStatus();
  };

  SemaphoreReservation w1_res;
  launchTaskOk(acquireTask(sem, &w1_res));
  DetachedHandle h2 = launch(acquireTask(sem), executor_, [](absl::Status) {});
  DetachedHandle h3 = launch(acquireTask(sem), executor_, [](absl::Status) {});
  drain(); // 3 waiters queued

  // Cancel trailing waiters h2 and h3
  h2.cancel();
  h3.cancel();

  // Release hold: processWaiters satisfies w1 and prunes cancelled waiters h2 and h3
  hold->release();
  drain();
  EXPECT_TRUE(w1_res.hasPermits());
  EXPECT_EQ(sem->currentPermits(), 5);
}

} // namespace
} // namespace Coroutine
} // namespace Envoy
