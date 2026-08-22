#include <memory>
#include <string>
#include <vector>

#include "source/common/coroutine/async_queue.h"
#include "source/common/coroutine/launch.h"
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

struct TestByteItem {
  std::string data;
};

struct TestByteSizeFunc {
  uint64_t operator()(const TestByteItem& item) const { return item.data.size(); }
};

class AsyncQueueTest : public testing::Test {
public:
  AsyncQueueTest() : executor_(std::make_shared<ManualExecutor>()) {}

  void drain() { executor_->drain(); }

  void launchTaskOk(Task<absl::Status> task) {
    handles_.push_back(
        launch(std::move(task), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  }

  template <typename T, typename SizeFunc, typename U = T>
  static Task<absl::Status> pushTask(AsyncQueue<T, SizeFunc>& queue, U item, bool* done = nullptr) {
    CO_RETURN_IF_ERROR(co_await queue.push(std::move(item)));
    if (done != nullptr) {
      *done = true;
    }
    co_return absl::OkStatus();
  }

  template <typename T, typename SizeFunc, typename U = T, typename TrackType = T>
  static Task<absl::Status> pushTrackTask(AsyncQueue<T, SizeFunc>& queue, U item,
                                          std::vector<TrackType>* order_vec) {
    TrackType tracked = item;
    CO_RETURN_IF_ERROR(co_await queue.push(std::move(item)));
    if (order_vec != nullptr) {
      order_vec->push_back(std::move(tracked));
    }
    co_return absl::OkStatus();
  }

  template <typename T, typename SizeFunc>
  static Task<absl::Status> popTask(AsyncQueue<T, SizeFunc>& queue,
                                    std::optional<T>* out_val = nullptr, bool* eof_seen = nullptr) {
    auto res = co_await queue.pop();
    CO_RETURN_IF_ERROR(res.status());
    if (res->has_value()) {
      if (out_val != nullptr) {
        *out_val = std::move(**res);
      }
    } else {
      if (eof_seen != nullptr) {
        *eof_seen = true;
      }
    }
    co_return absl::OkStatus();
  }

  template <typename T, typename SizeFunc, typename Container>
  static Task<absl::Status> popMultipleTask(AsyncQueue<T, SizeFunc>& queue, size_t count,
                                            Container* out_vec) {
    for (size_t i = 0; i < count; ++i) {
      auto val_or = co_await queue.pop();
      CO_RETURN_IF_ERROR(val_or.status());
      if (!val_or->has_value()) {
        break;
      }
      if (out_vec != nullptr) {
        out_vec->push_back(std::move(**val_or));
      }
    }
    co_return absl::OkStatus();
  }

  template <typename T, typename SizeFunc, typename U = T>
  void launchPush(AsyncQueue<T, SizeFunc>& queue, U item, bool* done = nullptr) {
    launchTaskOk(pushTask(queue, std::move(item), done));
  }

  template <typename T, typename SizeFunc, typename U = T, typename TrackType>
  void launchPushTrack(AsyncQueue<T, SizeFunc>& queue, U item, std::vector<TrackType>& order_vec) {
    launchTaskOk(pushTrackTask(queue, std::move(item), &order_vec));
  }

  template <typename T, typename SizeFunc>
  void launchPop(AsyncQueue<T, SizeFunc>& queue, std::optional<T>* out_val = nullptr,
                 bool* eof_seen = nullptr) {
    launchTaskOk(popTask(queue, out_val, eof_seen));
  }

  template <typename T, typename SizeFunc>
  void launchPop(AsyncQueue<T, SizeFunc>& queue, std::nullptr_t, bool* eof_seen) {
    launchTaskOk(popTask(queue, static_cast<std::optional<T>*>(nullptr), eof_seen));
  }

  template <typename T, typename SizeFunc>
  void launchPop(AsyncQueue<T, SizeFunc>& queue, std::optional<T>& out_val) {
    launchPop(queue, &out_val, nullptr);
  }

  template <typename T, typename SizeFunc, typename Container>
  void launchPopMultiple(AsyncQueue<T, SizeFunc>& queue, size_t count, Container& out_vec) {
    launchTaskOk(popMultipleTask(queue, count, &out_vec));
  }

  std::shared_ptr<ManualExecutor> executor_;
  std::vector<DetachedHandle> handles_;
};

// ============================================================================
// 1. Basic Push, Pop, and FIFO Ordering
// ============================================================================

TEST_F(AsyncQueueTest, UnboundedQueuePushPopFIFO) {
  AsyncQueue<std::string> queue;

  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.currentSize(), 0);
  EXPECT_EQ(queue.itemCount(), 0);

  bool push1_done = false;
  bool push2_done = false;
  bool push3_done = false;
  launchPush(queue, "item1", &push1_done);
  launchPush(queue, "item2", &push2_done);
  launchPush(queue, "item3", &push3_done);
  drain();
  EXPECT_TRUE(push1_done);
  EXPECT_TRUE(push2_done);
  EXPECT_TRUE(push3_done);
  EXPECT_EQ(queue.itemCount(), 3);
  EXPECT_EQ(queue.currentSize(), 3);

  std::vector<std::string> received;
  launchPopMultiple(queue, 3, received);
  drain();
  EXPECT_THAT(received, testing::ElementsAre("item1", "item2", "item3"));
  EXPECT_TRUE(queue.empty());
}

TEST_F(AsyncQueueTest, BoundedQueueBlocksPusherWhenFull) {
  // Capacity = 2 items
  AsyncQueue<int> queue(2);

  std::vector<int> pushed;
  auto push_task = [&queue, &pushed]() -> Task<absl::Status> {
    pushed.push_back(1);
    CO_RETURN_IF_ERROR(co_await queue.push(1));
    pushed.push_back(2);
    CO_RETURN_IF_ERROR(co_await queue.push(2));
    // 3rd push should suspend because capacity is 2
    pushed.push_back(3);
    CO_RETURN_IF_ERROR(co_await queue.push(3));
    pushed.push_back(4);
    CO_RETURN_IF_ERROR(co_await queue.push(4));
    co_return absl::OkStatus();
  };

  launchTaskOk(push_task());
  drain();

  // Pushes 1 and 2 completed; 3 is waiting in push_waiters_
  EXPECT_EQ(queue.itemCount(), 2);
  EXPECT_EQ(pushed.size(), 3);

  // Pop one item, which frees space and unblocks push of 3
  std::optional<int> pop1;
  launchPop(queue, pop1);
  drain();
  EXPECT_EQ(pop1, 1);

  // Now push of 3 has completed, and push of 4 suspended because queue is back to capacity (2,3)
  EXPECT_EQ(queue.itemCount(), 2);
  EXPECT_EQ(pushed.size(), 4);

  // Pop remaining items
  std::vector<int> remaining_popped;
  launchPopMultiple(queue, 3, remaining_popped);
  drain();
  EXPECT_THAT(remaining_popped, testing::ElementsAre(2, 3, 4));
  EXPECT_TRUE(queue.empty());
}

TEST_F(AsyncQueueTest, AbstractCapacityUnitBytes) {
  // Capacity = 100 bytes
  AsyncQueue<TestByteItem, TestByteSizeFunc> queue(100);

  EXPECT_EQ(queue.currentSize(), 0);

  bool push1_done = false;
  bool push2_done = false;

  // 60 bytes
  launchPush(queue, TestByteItem{std::string(60, 'a')}, &push1_done);
  // 50 bytes -> 60 + 50 = 110 > 100 bytes, suspends!
  launchPush(queue, TestByteItem{std::string(50, 'b')}, &push2_done);
  drain();

  EXPECT_TRUE(push1_done);
  EXPECT_FALSE(push2_done);
  EXPECT_EQ(queue.currentSize(), 60);

  // Pop first item (60 bytes)
  std::optional<TestByteItem> popped;
  launchPop(queue, popped);
  drain();

  ASSERT_TRUE(popped.has_value());
  EXPECT_EQ(popped->data.size(), 60);

  // Now push2 should have unblocked
  EXPECT_TRUE(push2_done);
  EXPECT_EQ(queue.currentSize(), 50);
  EXPECT_EQ(queue.itemCount(), 1);
}

TEST_F(AsyncQueueTest, DirectHandoffToWaitingPopper) {
  AsyncQueue<std::string> queue(1);

  std::optional<std::string> popped;
  launchPop(queue, popped);
  drain();
  EXPECT_FALSE(popped.has_value());

  // Push directly hands off to the waiting popper without queueing
  launchPush(queue, "direct_message");
  drain();
  EXPECT_TRUE(popped.has_value());
  EXPECT_EQ(*popped, "direct_message");
  EXPECT_TRUE(queue.empty());
}

TEST_F(AsyncQueueTest, TryPushAndTryPop) {
  AsyncQueue<int> queue(1);

  EXPECT_TRUE(queue.tryPush(10));
  // Queue full (capacity 1)
  EXPECT_FALSE(queue.tryPush(20));

  auto val = queue.tryPop();
  EXPECT_TRUE(val.has_value());
  EXPECT_EQ(*val, 10);

  // Queue now empty
  EXPECT_FALSE(queue.tryPop().has_value());
}

// ============================================================================
// 2. Close and Cancellation Semantics
// ============================================================================

TEST_F(AsyncQueueTest, CloseSignalsEOF) {
  AsyncQueue<std::string> queue;

  queue.tryPush("msg");
  queue.close();

  // Subsequent push fails
  EXPECT_FALSE(queue.tryPush("msg2"));

  // First pop gets queued item
  std::optional<std::string> val1;
  launchPop(queue, val1);
  drain();
  EXPECT_EQ(val1, "msg");

  // Second pop gets EOF (nullopt)
  bool eof_seen = false;
  launchPop(queue, nullptr, &eof_seen);
  drain();
  EXPECT_TRUE(eof_seen);
}

TEST_F(AsyncQueueTest, CloseAbortsPushWaiters) {
  AsyncQueue<int> queue(1);
  EXPECT_TRUE(queue.tryPush(1));

  absl::Status push_status;
  auto push_task = [&queue, &push_status]() -> Task<absl::Status> {
    push_status = co_await queue.push(2);
    co_return absl::OkStatus();
  };

  launchTaskOk(push_task());
  drain();

  queue.close();
  drain();

  EXPECT_THAT(push_status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
}

TEST_F(AsyncQueueTest, PopCancellationUnregistersWaiter) {
  AsyncQueue<std::string> queue;

  std::optional<absl::StatusOr<std::optional<std::string>>> pop_result;
  auto pop_task = [&queue, &pop_result]() -> Task<absl::Status> {
    pop_result = co_await queue.pop();
    co_return absl::OkStatus();
  };

  DetachedHandle handle =
      launch(pop_task(), executor_, [](absl::Status status) { EXPECT_OK(status); });
  drain();
  EXPECT_FALSE(pop_result.has_value());

  // Cancel the pop operation
  handle.cancel();
  ASSERT_TRUE(pop_result.has_value());
  EXPECT_TRUE(absl::IsCancelled(pop_result->status()));

  // Ensure pushing now doesn't send to cancelled waiter
  EXPECT_TRUE(queue.tryPush("item"));
  EXPECT_EQ(queue.itemCount(), 1);
}

TEST_F(AsyncQueueTest, PushCancellationUnregistersWaiter) {
  AsyncQueue<int> queue(1);
  EXPECT_TRUE(queue.tryPush(1)); // Fill queue

  std::optional<absl::Status> push_result;
  auto push_task = [&queue, &push_result]() -> Task<absl::Status> {
    push_result = co_await queue.push(2);
    co_return absl::OkStatus();
  };

  DetachedHandle handle =
      launch(push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); });
  drain();
  EXPECT_FALSE(push_result.has_value());

  // Cancel the push operation
  handle.cancel();
  ASSERT_TRUE(push_result.has_value());
  EXPECT_TRUE(absl::IsCancelled(*push_result));

  // Now pop item 1; the cancelled push shouldn't have enqueued item 2
  auto item = queue.tryPop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 1);
  EXPECT_TRUE(queue.empty());
}

// ============================================================================
// 3. Shared Capacity Across Multiple Queues
// ============================================================================

TEST_F(AsyncQueueTest, SharedCapacityAcrossMultipleQueues) {
  auto shared_cap = std::make_shared<SharedCapacity>(3);
  AsyncQueue<int> q1(shared_cap);
  AsyncQueue<int> q2(shared_cap);

  EXPECT_EQ(q1.capacity(), shared_cap);
  EXPECT_EQ(q2.capacity(), shared_cap);
  EXPECT_EQ(q1.maxSize(), 3);
  EXPECT_EQ(q2.maxSize(), 3);

  EXPECT_TRUE(q1.tryPush(10));
  EXPECT_TRUE(q2.tryPush(20));
  EXPECT_TRUE(q1.tryPush(30));

  // Total shared capacity (3 items) reached.
  EXPECT_EQ(shared_cap->currentSize(), 3);
  EXPECT_EQ(q1.currentSize(), 2);
  EXPECT_EQ(q2.currentSize(), 1);

  // Pushing into q2 should suspend
  bool q2_push_done = false;
  launchPush(q2, 40, &q2_push_done);
  drain();

  EXPECT_FALSE(q2_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 3);

  // Pop from q1, which frees 1 slot in shared capacity and unblocks q2's push
  auto pop1 = q1.tryPop();
  ASSERT_TRUE(pop1.has_value());
  EXPECT_EQ(*pop1, 10);

  drain();
  EXPECT_TRUE(q2_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 3);
  EXPECT_EQ(q1.currentSize(), 1);
  EXPECT_EQ(q2.currentSize(), 2);
}

TEST_F(AsyncQueueTest, SharedCapacityByteBudgetAcrossChainedQueues) {
  auto shared_cap = std::make_shared<SharedCapacity>(100);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q1(shared_cap);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q2(shared_cap);

  EXPECT_TRUE(q1.tryPush(TestByteItem{std::string(60, 'a')}));
  EXPECT_TRUE(q2.tryPush(TestByteItem{std::string(40, 'b')}));
  EXPECT_EQ(shared_cap->currentSize(), 100);

  // Push 30 bytes to q1 -> exceeds 100 byte limit, suspends
  bool q1_push_done = false;
  launchPush(q1, TestByteItem{std::string(30, 'c')}, &q1_push_done);
  drain();

  EXPECT_FALSE(q1_push_done);

  // Pop 40 bytes from q2
  auto pop_item = q2.tryPop();
  ASSERT_TRUE(pop_item.has_value());
  EXPECT_EQ(pop_item->data.size(), 40);

  drain();
  // q1 unblocks and completes
  EXPECT_TRUE(q1_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 90); // 60 + 30
}

TEST_F(AsyncQueueTest, SharedCapacityQueueDestructionReleasesCapacity) {
  auto shared_cap = std::make_shared<SharedCapacity>(2);
  auto q1 = std::make_unique<AsyncQueue<int>>(shared_cap);
  AsyncQueue<int> q2(shared_cap);

  EXPECT_TRUE(q1->tryPush(1));
  EXPECT_TRUE(q1->tryPush(2));
  EXPECT_EQ(shared_cap->currentSize(), 2);

  bool q2_push_done = false;
  launchPush(q2, 3, &q2_push_done);
  drain();
  EXPECT_FALSE(q2_push_done);

  // Destroying q1 releases its 2 units from shared capacity
  q1.reset();

  drain();
  EXPECT_TRUE(q2_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 1);
}

TEST_F(AsyncQueueTest, PushAndPopImmediateReturnAwaitableDirectly) {
  AsyncQueue<int> queue(10);
  EXPECT_EQ(queue.capacity()->maxSize(), 10);

  auto push_and_pop = [&queue]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await queue.push(42));
    auto item_or = co_await queue.pop();
    if (!item_or.ok() || !item_or->has_value() || **item_or != 42) {
      co_return absl::InternalError("unexpected pop");
    }
    co_return absl::OkStatus();
  };

  launchTaskOk(push_and_pop());
  drain();
  EXPECT_TRUE(queue.empty());
}

TEST_F(AsyncQueueTest, DirectHandoffBypassesSharedCapacityWhenFull) {
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  AsyncQueue<int> q1(shared_cap);
  AsyncQueue<int> q2(shared_cap);

  // Fill shared capacity using q1
  EXPECT_TRUE(q1.tryPush(100));
  EXPECT_EQ(shared_cap->currentSize(), 1);

  // Start waiting popper on q2
  std::optional<int> q2_popped;
  launchPop(q2, q2_popped);
  drain();
  EXPECT_FALSE(q2_popped.has_value());

  // q2.tryPush should succeed via direct handoff even though SharedCapacity is full
  EXPECT_TRUE(q2.tryPush(200));
  EXPECT_TRUE(q2_popped.has_value());
  EXPECT_EQ(*q2_popped, 200);
  EXPECT_EQ(shared_cap->currentSize(), 1);
  EXPECT_TRUE(q2.empty());
}

TEST_F(AsyncQueueTest, PopHandoffFromWaitingPusherWhenCapacityFull) {
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  AsyncQueue<int> q1(shared_cap);
  AsyncQueue<int> q2(shared_cap);

  // Fill capacity with q1
  EXPECT_TRUE(q1.tryPush(100));

  // Push on q2 suspends because shared capacity is full
  bool q2_push_done = false;
  launchPush(q2, 200, &q2_push_done);
  drain();
  EXPECT_FALSE(q2_push_done);

  // Pop on q2 should take directly from waiting pusher and unblock it immediately
  std::optional<int> val = q2.tryPop();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 200);
  EXPECT_TRUE(q2_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 1); // Held by q1
}

TEST_F(AsyncQueueTest, ChainedQueuesPipelineStreamingUnderCapacityConstraint) {
  // 3-stage pipeline sharing 1 capacity unit: Q1 -> F1 -> Q2 -> F2 -> Q3 -> Sink
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  AsyncQueue<int> q1(shared_cap);
  AsyncQueue<int> q2(shared_cap);
  AsyncQueue<int> q3(shared_cap);

  std::vector<int> sink_received;

  // Filter 1: pops from q1, multiplies by 10, pushes to q2
  auto filter1_task = [&q1, &q2]() -> Task<absl::Status> {
    while (true) {
      auto item_or = co_await q1.pop();
      if (!item_or.ok() || !item_or->has_value()) {
        q2.close();
        break;
      }
      CO_RETURN_IF_ERROR(co_await q2.push(**item_or * 10));
    }
    co_return absl::OkStatus();
  };

  // Filter 2: pops from q2, adds 1, pushes to q3
  auto filter2_task = [&q2, &q3]() -> Task<absl::Status> {
    while (true) {
      auto item_or = co_await q2.pop();
      if (!item_or.ok() || !item_or->has_value()) {
        q3.close();
        break;
      }
      CO_RETURN_IF_ERROR(co_await q3.push(**item_or + 1));
    }
    co_return absl::OkStatus();
  };

  // Sink: pops from q3, collects into sink_received
  auto sink_task = [&q3, &sink_received]() -> Task<absl::Status> {
    while (true) {
      auto item_or = co_await q3.pop();
      if (!item_or.ok() || !item_or->has_value()) {
        break;
      }
      sink_received.push_back(**item_or);
    }
    co_return absl::OkStatus();
  };

  launchTaskOk(filter1_task());
  launchTaskOk(filter2_task());
  launchTaskOk(sink_task());

  // Push 3 items into Q1
  for (int i = 1; i <= 3; ++i) {
    launchPush(q1, i);
  }

  drain();
  q1.close();
  drain();

  // (1*10+1=11, 2*10+1=21, 3*10+1=31)
  EXPECT_THAT(sink_received, testing::ElementsAre(11, 21, 31));
}

TEST_F(AsyncQueueTest, GlobalTemporalFIFOCapacityDistribution) {
  // Shared capacity limit = 1 unit
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  AsyncQueue<std::string> q1(shared_cap);
  AsyncQueue<std::string> q2(shared_cap);
  AsyncQueue<std::string> q3(shared_cap);

  // Initial fill
  EXPECT_TRUE(q1.tryPush("init"));
  EXPECT_EQ(shared_cap->currentSize(), 1);

  std::vector<std::string> order_granted;

  // Queue up 2 pushes per queue in interleaved temporal order:
  // q1_1, q2_1, q3_1, q1_2, q3_2, q2_2
  launchPushTrack(q1, "q1_1", order_granted);
  launchPushTrack(q2, "q2_1", order_granted);
  launchPushTrack(q3, "q3_1", order_granted);
  launchPushTrack(q1, "q1_2", order_granted);
  launchPushTrack(q3, "q3_2", order_granted);
  launchPushTrack(q2, "q2_2", order_granted);

  drain();
  EXPECT_TRUE(order_granted.empty());

  // Pop initial item to start granting capacity
  auto pop1 = q1.tryPop();
  ASSERT_TRUE(pop1.has_value());
  EXPECT_EQ(*pop1, "init");
  drain();

  // Pop whichever queue currently holds the item to free capacity for the next in global FIFO order
  for (int i = 0; i < 6; ++i) {
    if (!q1.empty()) {
      q1.tryPop();
    } else if (!q2.empty()) {
      q2.tryPop();
    } else if (!q3.empty()) {
      q3.tryPop();
    }
    drain();
  }

  // All 6 pushes should have been granted in strict temporal global FIFO order across queues:
  // q1_1 -> q2_1 -> q3_1 -> q1_2 -> q3_2 -> q2_2
  EXPECT_THAT(order_granted, testing::ElementsAre("q1_1", "q2_1", "q3_1", "q1_2", "q3_2", "q2_2"));
}

TEST_F(AsyncQueueTest, CancelCapacityRequestOnClose) {
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  auto q1 = std::make_unique<AsyncQueue<int>>(shared_cap);
  AsyncQueue<int> q2(shared_cap);

  EXPECT_TRUE(q1->tryPush(10));
  EXPECT_EQ(shared_cap->currentSize(), 1);

  // Close q1 while it still has items to pop
  q1->close();
  EXPECT_TRUE(q1->closed());

  // q2 tries to push and suspends
  bool q2_push_done = false;
  launchPush(q2, 20, &q2_push_done);
  drain();
  EXPECT_FALSE(q2_push_done);

  // Pop remaining item from closed q1 -> frees capacity
  auto val = q1->tryPop();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 10);

  drain();
  // q2 should have been granted capacity by processWaiters() and unblocked
  EXPECT_TRUE(q2_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 1);
}

TEST_F(AsyncQueueTest, HasCapacityOverflowProtection) {
  SharedCapacity cap(100);
  EXPECT_TRUE(cap.tryAcquire(50));
  EXPECT_TRUE(cap.hasCapacity(50));
  EXPECT_FALSE(cap.hasCapacity(51));
  EXPECT_FALSE(cap.hasCapacity(std::numeric_limits<uint64_t>::max()));
  EXPECT_FALSE(cap.hasCapacity(std::numeric_limits<uint64_t>::max() - 10));
}

TEST_F(AsyncQueueTest, NullCallbackInRequestCapacityDefensiveHandling) {
  SharedCapacity cap(10);
  EXPECT_EQ(cap.currentSize(), 0);

  // In debug builds, passing a null callback triggers ASSERT.
  EXPECT_DEBUG_DEATH({ cap.requestCapacity(5, nullptr); }, "assert failure: cb != nullptr");

  // Acquire capacity to fill it.
  EXPECT_TRUE(cap.tryAcquire(10));
  EXPECT_EQ(cap.currentSize(), 10);

  // Subsequent valid requests should operate normally and acquire capacity when released.
  bool valid_called = false;
  cap.requestCapacity(5, [&valid_called]() { valid_called = true; });
  EXPECT_FALSE(valid_called);

  cap.release(10); // Releases 10 units, triggers processWaiters(), grants req
  EXPECT_TRUE(valid_called);
  EXPECT_EQ(cap.currentSize(), 5);

  cap.release(5);
  EXPECT_EQ(cap.currentSize(), 0);
}

// ============================================================================
// 4. Global FIFO Ordering, Head-of-Line Blocking, and Starvation Prevention
// ============================================================================

TEST_F(AsyncQueueTest, GlobalFIFOOrderPreservedAcrossDynamicQueueDestruction) {
  // Shared capacity limit = 1 unit
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  auto q0 = std::make_unique<AsyncQueue<std::string>>(shared_cap);
  auto q1 = std::make_unique<AsyncQueue<std::string>>(shared_cap);
  auto q2 = std::make_unique<AsyncQueue<std::string>>(shared_cap);

  // Fill capacity with q0
  EXPECT_TRUE(q0->tryPush("q0_init"));
  EXPECT_EQ(shared_cap->currentSize(), 1);

  std::vector<std::string> order_granted;

  // Launch pushes in order: q0, q1, q2
  launchPushTrack(*q0, "q0_push", order_granted);
  launchPushTrack(*q1, "q1_push", order_granted);
  launchPushTrack(*q2, "q2_push", order_granted);

  drain();
  EXPECT_TRUE(order_granted.empty());

  // Pop q0_init: frees capacity and grants space to the head waiter (q0_push).
  auto pop0 = q0->tryPop();
  ASSERT_TRUE(pop0.has_value());
  EXPECT_EQ(*pop0, "q0_init");
  drain();

  EXPECT_THAT(order_granted, testing::ElementsAre("q0_push"));

  // Now destroy q0. Destroying q0 cleans up its resources without disrupting
  // the wait queue. In global FIFO order, the next waiter to receive capacity
  // MUST be q1_push, not q2_push.
  q0.reset();
  drain();

  // Add another push to q1 to test ordering: it queues at the tail behind q2_push.
  launchPushTrack(*q1, "q1_push2", order_granted);
  drain();

  // q1_push was at the head of the wait queue, so it gets granted next.
  EXPECT_THAT(order_granted, testing::ElementsAre("q0_push", "q1_push"));

  // Now pop q1's item -> frees capacity; q2_push was ahead of q1_push2 in FIFO order,
  // so q2_push MUST get space next, NOT q1_push2.
  auto pop1 = q1->tryPop();
  ASSERT_TRUE(pop1.has_value());
  EXPECT_EQ(*pop1, "q1_push");
  drain();

  EXPECT_THAT(order_granted, testing::ElementsAre("q0_push", "q1_push", "q2_push"));

  // Now pop q2's item -> frees capacity; q1_push2 is now at the head and gets space next.
  auto pop2 = q2->tryPop();
  ASSERT_TRUE(pop2.has_value());
  EXPECT_EQ(*pop2, "q2_push");
  drain();

  EXPECT_THAT(order_granted, testing::ElementsAre("q0_push", "q1_push", "q2_push", "q1_push2"));
}

TEST_F(AsyncQueueTest, DirectHandoffInTryPopUnblocksSubsequentWaiters) {
  // Shared capacity limit = 10 units
  auto shared_cap = std::make_shared<SharedCapacity>(10);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q1(shared_cap);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q2(shared_cap);

  // Fill 5 units in q1 -> shared_cap has 5 units free
  EXPECT_TRUE(q1.tryPush(TestByteItem{std::string(5, 'a')}));
  EXPECT_EQ(shared_cap->currentSize(), 5);

  bool push1_done = false;
  bool push2_done = false;

  // Push 1 on q2: 20 bytes -> exceeds 10 bytes capacity, suspends
  launchPush(q2, TestByteItem{std::string(20, 'b')}, &push1_done);
  // Push 2 on q2: 2 bytes -> would fit in 5 free units, but suspended behind push 1
  launchPush(q2, TestByteItem{std::string(2, 'c')}, &push2_done);
  drain();

  EXPECT_FALSE(push1_done);
  EXPECT_FALSE(push2_done);

  // Direct handoff pop from q2: pops push 1 directly without using capacity
  auto popped = q2.tryPop();
  ASSERT_TRUE(popped.has_value());
  EXPECT_EQ(popped->data.size(), 20);

  drain();

  EXPECT_TRUE(push1_done);
  // Push 2 must now be unblocked immediately because it fits within the 5 free units
  EXPECT_TRUE(push2_done);
  EXPECT_EQ(q2.itemCount(), 1);
  EXPECT_EQ(shared_cap->currentSize(), 7); // 5 + 2
}

TEST_F(AsyncQueueTest, PushCancellationUnblocksSubsequentWaiters) {
  // Shared capacity limit = 10 units
  auto shared_cap = std::make_shared<SharedCapacity>(10);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q1(shared_cap);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q2(shared_cap);

  // Fill 5 units in q1 -> shared_cap has 5 units free
  EXPECT_TRUE(q1.tryPush(TestByteItem{std::string(5, 'a')}));
  EXPECT_EQ(shared_cap->currentSize(), 5);

  std::optional<absl::Status> push1_status;
  bool push2_done = false;

  // Push 1 on q2: 20 bytes -> exceeds 10 bytes capacity, suspends
  auto push1_task = [&q2, &push1_status]() -> Task<absl::Status> {
    push1_status = co_await q2.push(TestByteItem{std::string(20, 'b')});
    co_return *push1_status;
  };

  DetachedHandle h1 = launch(push1_task(), executor_,
                             [](absl::Status status) { EXPECT_TRUE(absl::IsCancelled(status)); });
  // Push 2 on q2: 3 bytes -> would fit in 5 free units, but suspended behind push 1
  launchPush(q2, TestByteItem{std::string(3, 'c')}, &push2_done);
  drain();

  EXPECT_FALSE(push1_status.has_value());
  EXPECT_FALSE(push2_done);

  // Cancel push 1
  h1.cancel();
  ASSERT_TRUE(push1_status.has_value());
  EXPECT_TRUE(absl::IsCancelled(*push1_status));

  drain();

  // Push 2 should now be unblocked immediately
  EXPECT_TRUE(push2_done);
  EXPECT_EQ(q2.itemCount(), 1);
  EXPECT_EQ(shared_cap->currentSize(), 8); // 5 + 3
}

TEST_F(AsyncQueueTest, LargeChunkAntiStarvation) {
  // Shared capacity limit = 100 bytes
  auto shared_cap = std::make_shared<SharedCapacity>(100);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q1(shared_cap);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q2(shared_cap);

  // Fill 60 bytes in q1 (40 bytes free in shared_cap)
  EXPECT_TRUE(q1.tryPush(TestByteItem{std::string(60, 'a')}));
  EXPECT_EQ(shared_cap->currentSize(), 60);

  bool large_push_done = false;
  bool small_push_done = false;

  // Waiter 1 (q1): large chunk of 80 bytes (exceeds 40 free bytes, so suspends)
  launchPush(q1, TestByteItem{std::string(80, 'b')}, &large_push_done);
  // Waiter 2 (q2): small chunk of 20 bytes (would fit into 40 free bytes, but behind Waiter 1 in
  // FIFO)
  launchPush(q2, TestByteItem{std::string(20, 'c')}, &small_push_done);
  drain();

  // Strict Head-of-Line FIFO: small push MUST NOT bypass the large push ahead of it
  EXPECT_FALSE(large_push_done);
  EXPECT_FALSE(small_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 60);

  // Pop 60 bytes from q1 -> current_size becomes 0 -> 100 bytes free
  auto item = q1.tryPop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(item->data.size(), 60);

  drain();

  // Now both should have been granted in order:
  // 1. Large chunk (80 bytes) granted first -> currentSize becomes 80
  // 2. Small chunk (20 bytes) granted second -> currentSize becomes 100 (80 + 20)
  EXPECT_TRUE(large_push_done);
  EXPECT_TRUE(small_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 100);
  EXPECT_EQ(q1.itemCount(), 1);
  EXPECT_EQ(q2.itemCount(), 1);
}

TEST_F(AsyncQueueTest, OversizedChunkAdmissionAfterDrain) {
  // Shared capacity limit = 50 bytes
  auto shared_cap = std::make_shared<SharedCapacity>(50);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q1(shared_cap);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q2(shared_cap);

  // q1 holds 30 bytes
  EXPECT_TRUE(q1.tryPush(TestByteItem{std::string(30, 'a')}));
  EXPECT_EQ(shared_cap->currentSize(), 30);

  bool oversized_push_done = false;
  bool normal_push_done = false;

  // Waiter 1 (q2): oversized chunk of 100 bytes (> maxSize 50).
  // Because current_size == 30 > 0, it suspends and waits in the wait queue.
  launchPush(q2, TestByteItem{std::string(100, 'b')}, &oversized_push_done);

  // Waiter 2 (q1): normal chunk of 10 bytes (behind oversized chunk in wait queue)
  launchPush(q1, TestByteItem{std::string(10, 'c')}, &normal_push_done);
  drain();

  EXPECT_FALSE(oversized_push_done);
  EXPECT_FALSE(normal_push_done);

  // Pop 30 bytes from q1 -> current_size becomes 0
  auto item1 = q1.tryPop();
  ASSERT_TRUE(item1.has_value());
  EXPECT_EQ(item1->data.size(), 30);

  drain();

  // Oversized chunk is granted when current_size == 0
  EXPECT_TRUE(oversized_push_done);
  // Normal chunk is still waiting because current_size is 100 >= 50
  EXPECT_FALSE(normal_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 100);

  // Pop the oversized chunk from q2 -> current_size becomes 0
  auto item2 = q2.tryPop();
  ASSERT_TRUE(item2.has_value());
  EXPECT_EQ(item2->data.size(), 100);

  drain();

  // Now normal chunk of 10 bytes is admitted
  EXPECT_TRUE(normal_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 10);
  EXPECT_EQ(q1.itemCount(), 1);
}

TEST_F(AsyncQueueTest, CancellationOfHeadAndMiddleWaitersInGlobalWaitlist) {
  // Shared capacity limit = 10 units
  auto shared_cap = std::make_shared<SharedCapacity>(10);
  AsyncQueue<int> q1(shared_cap);
  AsyncQueue<int> q2(shared_cap);
  AsyncQueue<int> q3(shared_cap);

  // Fill 10 units in q1
  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(q1.tryPush(i));
  }
  EXPECT_EQ(shared_cap->currentSize(), 10);

  // Queue up 3 waiters in SharedCapacity:
  // Waiter 1 (Head, q2): push 1 item
  // Waiter 2 (Middle, q3): push 1 item
  // Waiter 3 (Tail, q2): push 1 item
  bool w1_done = false;
  std::optional<absl::Status> w2_status;
  bool w3_done = false;

  launchPush(q2, 101, &w1_done);

  auto w2_task = [&q3, &w2_status]() -> Task<absl::Status> {
    w2_status = co_await q3.push(102);
    co_return *w2_status;
  };

  launchPush(q2, 103, &w3_done);

  DetachedHandle h2 = launch(w2_task(), executor_,
                             [](absl::Status status) { EXPECT_TRUE(absl::IsCancelled(status)); });
  drain();

  EXPECT_FALSE(w1_done);
  EXPECT_FALSE(w2_status.has_value());
  EXPECT_FALSE(w3_done);

  // Cancel middle waiter (w2)
  h2.cancel();
  ASSERT_TRUE(w2_status.has_value());
  EXPECT_TRUE(absl::IsCancelled(*w2_status));

  // Pop 1 item from q1 -> frees 1 unit -> w1 (head) should be granted
  EXPECT_TRUE(q1.tryPop().has_value());
  drain();

  EXPECT_TRUE(w1_done);
  EXPECT_FALSE(w3_done);

  // Pop 1 item from q1 -> frees 1 unit -> w3 (tail) should be granted (since w2 was cancelled)
  EXPECT_TRUE(q1.tryPop().has_value());
  drain();

  EXPECT_TRUE(w3_done);
  EXPECT_EQ(q2.itemCount(), 2);
  EXPECT_EQ(q3.itemCount(), 0);

  // Now test cancelling head waiter when partial capacity is available:
  // Free 3 units in q1
  EXPECT_TRUE(q1.tryPop().has_value());
  EXPECT_TRUE(q1.tryPop().has_value());
  EXPECT_TRUE(q1.tryPop().has_value());
  // Currently shared_cap has 5 units in q1 + 2 units in q2 = 7 units. (3 units free).

  // Head waiter needs 5 units (cannot be granted, 5 > 3 free)
  AsyncQueue<TestByteItem, TestByteSizeFunc> q_bytes(shared_cap);
  std::optional<absl::Status> head_status;
  auto head_task = [&q_bytes, &head_status]() -> Task<absl::Status> {
    head_status = co_await q_bytes.push(TestByteItem{std::string(5, 'x')});
    co_return *head_status;
  };

  DetachedHandle h_head = launch(
      head_task(), executor_, [](absl::Status status) { EXPECT_TRUE(absl::IsCancelled(status)); });
  // Next waiter needs 2 units (fits within 3 free units, but blocked by head)
  bool next_done = false;
  launchPush(q_bytes, TestByteItem{std::string(2, 'y')}, &next_done);
  drain();

  EXPECT_FALSE(head_status.has_value());
  EXPECT_FALSE(next_done);

  // Cancel head waiter: next waiter should be granted immediately without popping!
  h_head.cancel();
  ASSERT_TRUE(head_status.has_value());
  EXPECT_TRUE(absl::IsCancelled(*head_status));

  drain();
  EXPECT_TRUE(next_done);
}

// ============================================================================
// 5. Reentrancy, Lifecycle, and Complex Destruction Edge Cases
// ============================================================================

TEST_F(AsyncQueueTest, ReentrantReleaseDoesNotLoseWakeups) {
  // Shared capacity limit = 2
  auto shared_cap = std::make_shared<SharedCapacity>(2);
  AsyncQueue<int> q1(shared_cap);
  AsyncQueue<int> q2(shared_cap);

  // Fill capacity with 2 items in q1
  EXPECT_TRUE(q1.tryPush(1));
  EXPECT_TRUE(q1.tryPush(2));
  EXPECT_EQ(shared_cap->currentSize(), 2);

  // Launch pusher 1 on q1 (suspended)
  bool q1_push3_done = false;
  auto q1_push_task = [&q1, &q1_push3_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q1.push(3));
    // When q1 unblocks this push, immediately pop an item, triggering reentrant release!
    auto popped = q1.tryPop();
    EXPECT_TRUE(popped.has_value());
    q1_push3_done = true;
    co_return absl::OkStatus();
  };

  // Launch pusher on q2 (suspended)
  bool q2_push_done = false;

  launchTaskOk(q1_push_task());
  launchPush(q2, 100, &q2_push_done);
  drain();

  EXPECT_FALSE(q1_push3_done);
  EXPECT_FALSE(q2_push_done);

  // Pop from q1: frees 1 slot.
  // This triggers processWaiters() -> unblocks q1_push_task.
  // q1_push_task runs and performs a re-entrant pop from q1 -> frees another slot while
  // processWaiters() is running! q2_push_task must also be unblocked without lost wakeup.
  auto item = q1.tryPop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 1);

  drain();

  EXPECT_TRUE(q1_push3_done);
  EXPECT_TRUE(q2_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 2);
}

TEST_F(AsyncQueueTest, DestructionReleasesCapacityBeforeAbortingWaiters) {
  auto shared_cap = std::make_shared<SharedCapacity>(2);
  auto q1 = std::make_unique<AsyncQueue<int>>(shared_cap);
  AsyncQueue<int> q2(shared_cap);

  EXPECT_TRUE(q1->tryPush(1));
  EXPECT_TRUE(q1->tryPush(2));
  EXPECT_EQ(shared_cap->currentSize(), 2);

  uint64_t cap_seen_in_error_handler = 999;
  absl::Status q1_error_status;
  auto q1_push_task = [&q1, &cap_seen_in_error_handler, &shared_cap,
                       &q1_error_status]() -> Task<absl::Status> {
    auto status = co_await q1->push(3);
    q1_error_status = status;
    if (!status.ok()) {
      // In error handler, check what capacity is reported
      cap_seen_in_error_handler = shared_cap->currentSize();
    }
    co_return status;
  };

  bool q2_push_done = false;
  launchPush(q2, 10, &q2_push_done);

  handles_.push_back(launch(q1_push_task(), executor_, [](absl::Status status) {
    EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
  }));
  drain();

  // Reset q1, destroying it
  q1.reset();
  drain();

  EXPECT_THAT(q1_error_status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
  // When q1's error handler ran, q1's capacity had already been released back to shared_cap
  // (q2 then took 1 unit, so cap_seen_in_error_handler should be <= 1, definitely NOT 2)
  EXPECT_LT(cap_seen_in_error_handler, 2);
  EXPECT_TRUE(q2_push_done);
}

TEST_F(AsyncQueueTest, SelfDestructionDuringPushUnblock) {
  auto q = std::make_unique<AsyncQueue<int>>(1);
  EXPECT_TRUE(q->tryPush(1)); // Fill queue

  bool push_completed = false;
  auto push_task = [&q, &push_completed]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q->push(2));
    push_completed = true;
    // Destroy the queue immediately upon unblock
    q.reset();
    co_return absl::OkStatus();
  };

  launchTaskOk(push_task());
  drain();
  EXPECT_FALSE(push_completed);

  // Pop item 1: this unblocks pusher for item 2, which then immediately calls q.reset()!
  // This must execute safely without Use-After-Free.
  auto val = q->tryPop();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 1);

  drain();
  EXPECT_TRUE(push_completed);
  EXPECT_EQ(q, nullptr);
}

TEST_F(AsyncQueueTest, SelfDestructionDuringClose) {
  auto q = std::make_unique<AsyncQueue<int>>(1);
  EXPECT_TRUE(q->tryPush(1)); // Fill queue

  bool handler_ran = false;
  auto push_task = [&q, &handler_ran]() -> Task<absl::Status> {
    auto status = co_await q->push(2);
    if (!status.ok()) {
      handler_ran = true;
      // Reset queue on close error
      q.reset();
    }
    co_return status;
  };

  handles_.push_back(launch(push_task(), executor_, [](absl::Status status) {
    EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
  }));
  drain();
  EXPECT_FALSE(handler_ran);

  // Close queue, which aborts push waiter, which then deletes q
  q->close();
  drain();

  EXPECT_TRUE(handler_ran);
  EXPECT_EQ(q, nullptr);
}

TEST_F(AsyncQueueTest, DynamicQueueCreationDuringGrant) {
  auto shared_cap = std::make_shared<SharedCapacity>(2);
  auto q1 = std::make_unique<AsyncQueue<int>>(shared_cap);
  EXPECT_TRUE(q1->tryPush(1));
  EXPECT_TRUE(q1->tryPush(2));

  std::unique_ptr<AsyncQueue<int>> dynamic_q;
  bool dynamic_push_done = false;

  auto push_task = [&q1, &shared_cap, &dynamic_q, &dynamic_push_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q1->push(3));
    // Create a new AsyncQueue dynamically while processWaiters() was running
    dynamic_q = std::make_unique<AsyncQueue<int>>(shared_cap);
    // Queue is full (items 2 and 3 in q1), so pushing into dynamic_q suspends
    CO_RETURN_IF_ERROR(co_await dynamic_q->push(100));
    dynamic_push_done = true;
    co_return absl::OkStatus();
  };

  launchTaskOk(push_task());
  drain();
  EXPECT_FALSE(dynamic_push_done);

  // Pop item 1 from q1 -> unblocks push(3) to q1, dynamic_q created and waits on push(100)
  EXPECT_TRUE(q1->tryPop().has_value());
  drain();
  EXPECT_FALSE(dynamic_push_done);
  ASSERT_NE(dynamic_q, nullptr);

  // Pop item 2 from q1 -> frees capacity -> dynamic_q is granted capacity and unblocks
  EXPECT_TRUE(q1->tryPop().has_value());
  drain();

  EXPECT_TRUE(dynamic_push_done);
  EXPECT_EQ(dynamic_q->itemCount(), 1);
  auto item = dynamic_q->tryPop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 100);
}

TEST_F(AsyncQueueTest, DirectHandoffAndQueueDestructionCleanup) {
  // Shared capacity limit = 5 units
  auto shared_cap = std::make_shared<SharedCapacity>(5);
  AsyncQueue<int> q1(shared_cap);
  auto q2 = std::make_unique<AsyncQueue<int>>(shared_cap);
  AsyncQueue<int> q3(shared_cap);

  // Fill 5 units in q1
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(q1.tryPush(i));
  }
  EXPECT_EQ(shared_cap->currentSize(), 5);

  bool q2_w1_done = false;
  absl::Status q2_w2_status;
  bool q3_w_done = false;

  // q2 pushes w1
  launchPush(*q2, 10, &q2_w1_done);

  // q2 pushes w2
  auto q2_w2_task = [&q2, &q2_w2_status]() -> Task<absl::Status> {
    q2_w2_status = co_await q2->push(20);
    co_return q2_w2_status;
  };
  handles_.push_back(launch(q2_w2_task(), executor_, [](absl::Status status) {
    EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
  }));

  // q3 pushes w3
  launchPush(q3, 30, &q3_w_done);
  drain();

  EXPECT_FALSE(q2_w1_done);
  EXPECT_FALSE(q3_w_done);

  // Direct handoff in q2: takes q2_w1 directly without acquiring capacity
  auto popped = q2->tryPop();
  ASSERT_TRUE(popped.has_value());
  EXPECT_EQ(*popped, 10);
  EXPECT_TRUE(q2_w1_done);
  EXPECT_EQ(shared_cap->currentSize(), 5); // Capacity unchanged (held by q1)

  // Destroy q2: cancels q2_w2 and aborts it with precondition error
  q2.reset();
  drain();
  EXPECT_THAT(q2_w2_status, HasStatusCode(absl::StatusCode::kFailedPrecondition));

  // Now pop 1 item from q1 -> frees 1 unit -> q3_w should be granted
  EXPECT_TRUE(q1.tryPop().has_value());
  drain();

  EXPECT_TRUE(q3_w_done);
  EXPECT_EQ(q3.itemCount(), 1);
  EXPECT_EQ(shared_cap->currentSize(), 5); // 4 in q1 + 1 in q3
}

TEST_F(AsyncQueueTest, DestructionWithMultiplePendingPushWaitersDoesNotGrantCapacityToDyingQueue) {
  // Shared capacity limit = 10 units
  auto shared_cap = std::make_shared<SharedCapacity>(10);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q1(shared_cap);
  auto q2 = std::make_unique<AsyncQueue<TestByteItem, TestByteSizeFunc>>(shared_cap);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q3(shared_cap);

  // q1 holds 1 unit (9 units free)
  EXPECT_TRUE(q1.tryPush(TestByteItem{std::string(1, 'a')}));
  EXPECT_EQ(shared_cap->currentSize(), 1);

  absl::Status q2_w1_status;
  absl::Status q2_w2_status;
  bool q3_w_done = false;

  // q2 pushes w1: 15 units (oversized / exceeds 9 free units, suspends at head of wait queue)
  auto q2_w1_task = [&q2, &q2_w1_status]() -> Task<absl::Status> {
    q2_w1_status = co_await q2->push(TestByteItem{std::string(15, 'b')});
    co_return q2_w1_status;
  };

  // q2 pushes w2: 2 units (would fit in 9 free units, but suspended behind w1)
  auto q2_w2_task = [&q2, &q2_w2_status]() -> Task<absl::Status> {
    q2_w2_status = co_await q2->push(TestByteItem{std::string(2, 'c')});
    co_return q2_w2_status;
  };

  handles_.push_back(launch(q2_w1_task(), executor_, [](absl::Status status) {
    EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
  }));
  handles_.push_back(launch(q2_w2_task(), executor_, [](absl::Status status) {
    EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
  }));
  // q3 pushes w3: 2 units (would fit in 9 free units, but suspended behind q2's waiters)
  launchPush(q3, TestByteItem{std::string(2, 'd')}, &q3_w_done);
  drain();

  EXPECT_FALSE(q3_w_done);

  // Destroy q2.
  // Both q2_w1 and q2_w2 must be cancelled from SharedCapacity without w2 being granted to q2,
  // and q3_w must be granted the capacity immediately upon unblocking the head!
  q2.reset();
  drain();

  EXPECT_THAT(q2_w1_status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(q2_w2_status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
  EXPECT_TRUE(q3_w_done);
  EXPECT_EQ(q3.itemCount(), 1);
  EXPECT_EQ(shared_cap->currentSize(), 3); // 1 in q1 + 2 in q3
}

TEST_F(AsyncQueueTest, DeepReentrantPushPopChainsAcrossMultipleQueues) {
  // Shared capacity limit = 4 units across 3 queues
  auto shared_cap = std::make_shared<SharedCapacity>(4);
  AsyncQueue<int> q1(shared_cap);
  AsyncQueue<int> q2(shared_cap);
  AsyncQueue<int> q3(shared_cap);

  // Fill capacity: 2 in q1, 2 in q2 -> total 4
  EXPECT_TRUE(q1.tryPush(1));
  EXPECT_TRUE(q1.tryPush(2));
  EXPECT_TRUE(q2.tryPush(10));
  EXPECT_TRUE(q2.tryPush(20));
  EXPECT_EQ(shared_cap->currentSize(), 4);

  std::vector<int> execution_order;

  // Coroutine on q1: pushes 3, upon grant pops from q1 and pushes 30 to q2
  auto task1 = [&]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q1.push(3));
    execution_order.push_back(1);
    auto p1 = q1.tryPop();
    EXPECT_TRUE(p1.has_value());
    CO_RETURN_IF_ERROR(co_await q2.push(30));
    execution_order.push_back(2);
    co_return absl::OkStatus();
  };

  // Coroutine on q2: pushes 40, upon grant pops from q2 and pushes 100 to q3
  auto task2 = [&]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q2.push(40));
    execution_order.push_back(3);
    auto p2 = q2.tryPop();
    EXPECT_TRUE(p2.has_value());
    CO_RETURN_IF_ERROR(co_await q3.push(100));
    execution_order.push_back(4);
    co_return absl::OkStatus();
  };

  // Coroutine on q3: pushes 200, upon grant pops from q3
  auto task3 = [&]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q3.push(200));
    execution_order.push_back(5);
    auto p3 = q3.tryPop();
    EXPECT_TRUE(p3.has_value());
    co_return absl::OkStatus();
  };

  launchTaskOk(task1());
  launchTaskOk(task2());
  launchTaskOk(task3());
  drain();

  EXPECT_TRUE(execution_order.empty());

  // Pop from q1: frees 1 slot -> triggers cascade of deep re-entrant grants across q1, q2, q3
  auto item = q1.tryPop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 1);

  drain();

  // Tasks 1, 2, 3 have their initial pushes granted in order (1, 3, 5).
  // Then Task 1's re-entrant push to q2 is granted (2).
  // Task 2's re-entrant push to q3 is now waiting at the head of the wait queue.
  EXPECT_THAT(execution_order, testing::ElementsAre(1, 3, 5, 2));

  // Pop from q2: frees 1 slot -> Task 2's re-entrant push to q3 is granted (4)
  auto item2 = q2.tryPop();
  ASSERT_TRUE(item2.has_value());

  drain();

  EXPECT_THAT(execution_order, testing::ElementsAre(1, 3, 5, 2, 4));
}

TEST_F(AsyncQueueTest, QueueDestructionDuringPushCancellationUnblock) {
  // Shared capacity = 2 units
  auto shared_cap = std::make_shared<SharedCapacity>(2);
  auto q1 = std::make_unique<AsyncQueue<TestByteItem, TestByteSizeFunc>>(shared_cap);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q2(shared_cap);

  // q1 holds 1 unit (1 unit free)
  EXPECT_TRUE(q1->tryPush(TestByteItem{std::string(1, 'a')}));
  EXPECT_EQ(shared_cap->currentSize(), 1);

  // q1 pushes 2 units (exceeds 1 free unit, suspends at head of SharedCapacity)
  std::optional<absl::Status> q1_push_status;
  auto q1_push_task = [&q1, &q1_push_status]() -> Task<absl::Status> {
    q1_push_status = co_await q1->push(TestByteItem{std::string(2, 'b')});
    co_return *q1_push_status;
  };

  // q2 pushes 1 unit (would fit into 1 free unit, but queued behind q1)
  bool q2_push_done = false;
  auto q2_push_task = [&q1, &q2, &q2_push_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q2.push(TestByteItem{std::string(1, 'c')}));
    q2_push_done = true;
    // Destroy q1 when unblocked
    q1.reset();
    co_return absl::OkStatus();
  };

  DetachedHandle h1 = launch(q1_push_task(), executor_,
                             [](absl::Status status) { EXPECT_TRUE(absl::IsCancelled(status)); });
  launchTaskOk(q2_push_task());
  drain();

  EXPECT_FALSE(q1_push_status.has_value());
  EXPECT_FALSE(q2_push_done);

  // Cancel q1's push: this cancels q1 from head of SharedCapacity, which unblocks q2's push.
  // q2's push immediately destroys q1.
  // removePushWaiter must safely handle q1 having been destroyed during cancellation!
  h1.cancel();
  ASSERT_TRUE(q1_push_status.has_value());
  EXPECT_TRUE(absl::IsCancelled(*q1_push_status));

  drain();
  EXPECT_TRUE(q2_push_done);
  EXPECT_EQ(q1, nullptr);
  EXPECT_EQ(shared_cap->currentSize(), 1);
}

TEST_F(AsyncQueueTest, SharedCapacityDirectAcquireRelease) {
  auto cap = std::make_shared<SharedCapacity>(10);
  EXPECT_EQ(cap->currentSize(), 0);
  EXPECT_TRUE(cap->hasCapacity(10));
  EXPECT_FALSE(cap->hasCapacity(11));

  EXPECT_TRUE(cap->tryAcquire(4));
  EXPECT_EQ(cap->currentSize(), 4);

  EXPECT_TRUE(cap->tryAcquire(6));
  EXPECT_EQ(cap->currentSize(), 10);

  // Capacity full
  EXPECT_FALSE(cap->tryAcquire(1));

  cap->release(6);
  EXPECT_EQ(cap->currentSize(), 4);

  cap->release(4);
  EXPECT_EQ(cap->currentSize(), 0);
}

TEST_F(AsyncQueueTest, SharedCapacityRequestAndCancel) {
  auto cap = std::make_shared<SharedCapacity>(5);
  EXPECT_TRUE(cap->tryAcquire(5));
  EXPECT_EQ(cap->currentSize(), 5);

  bool granted = false;
  auto it = cap->requestCapacity(3, [&granted]() { granted = true; });
  EXPECT_FALSE(granted);

  // Cancel the request
  cap->cancelRequest(it);

  // Free capacity: since request was cancelled, no callback invoked
  cap->release(5);
  EXPECT_FALSE(granted);
  EXPECT_EQ(cap->currentSize(), 0);

  // Now queue a new request and grant it
  EXPECT_TRUE(cap->tryAcquire(5));
  cap->requestCapacity(3, [&granted]() { granted = true; });
  EXPECT_FALSE(granted);

  // Release: grants it2
  cap->release(5);
  EXPECT_TRUE(granted);
  EXPECT_EQ(cap->currentSize(), 3);

  cap->release(3);
  EXPECT_EQ(cap->currentSize(), 0);
}

TEST_F(AsyncQueueTest, SharedCapacityOversizedOccupancyHasCapacity) {
  SharedCapacity cap(10);
  // Allowed when empty even if larger than max_size
  EXPECT_TRUE(cap.tryAcquire(15));
  EXPECT_EQ(cap.currentSize(), 15);
  // current_size_ > max_size_, hasCapacity must return false
  EXPECT_FALSE(cap.hasCapacity(1));
  EXPECT_FALSE(cap.hasCapacity(0));
}

TEST_F(AsyncQueueTest, SharedCapacityReleaseUnderflowAssert) {
  SharedCapacity cap(10);
  EXPECT_TRUE(cap.tryAcquire(5));
  EXPECT_DEBUG_DEATH({ cap.release(10); }, "assert failure: current_size_ >= size");
}

TEST_F(AsyncQueueTest, SharedCapacityCancelMiddleWaiter) {
  SharedCapacity cap(10);
  EXPECT_TRUE(cap.tryAcquire(10));

  bool g1 = false, g2 = false, g3 = false;
  cap.requestCapacity(5, [&g1]() { g1 = true; });
  auto w2 = cap.requestCapacity(5, [&g2]() { g2 = true; });
  cap.requestCapacity(5, [&g3]() { g3 = true; });

  // Cancel middle waiter (is_head == false)
  cap.cancelRequest(w2);
  EXPECT_FALSE(g1);
  EXPECT_FALSE(g2);
  EXPECT_FALSE(g3);

  // Release 5 units -> grants w1
  cap.release(5);
  EXPECT_TRUE(g1);
  EXPECT_FALSE(g2);
  EXPECT_FALSE(g3);

  // Release another 5 units -> grants w3 (w2 was skipped)
  cap.release(5);
  EXPECT_TRUE(g3);
  EXPECT_FALSE(g2);
}

TEST_F(AsyncQueueTest, SharedCapacitySelfDestructionInCallback) {
  auto cap = std::make_shared<SharedCapacity>(10);
  EXPECT_TRUE(cap->tryAcquire(10));

  bool granted = false;
  cap->requestCapacity(5, [&cap, &granted]() {
    granted = true;
    cap.reset(); // Destroy SharedCapacity while inside processWaiters()
  });

  cap->release(10);
  EXPECT_TRUE(granted);
  EXPECT_EQ(cap, nullptr);
}

TEST_F(AsyncQueueTest, SharedCapacityReentrancyInCallback) {
  SharedCapacity cap(10);
  EXPECT_TRUE(cap.tryAcquire(10));

  bool g1 = false, g2 = false;
  cap.requestCapacity(5, [&cap, &g1]() {
    g1 = true;
    // Synchronously release capacity from inside the grant callback
    cap.release(5);
  });
  cap.requestCapacity(5, [&g2]() { g2 = true; });

  cap.release(10);
  EXPECT_TRUE(g1);
  EXPECT_TRUE(g2);
  EXPECT_EQ(cap.currentSize(), 5);
}

TEST_F(AsyncQueueTest, AsyncQueueNullSharedCapacityPointerFallback) {
  AsyncQueue<int> q(nullptr);
  EXPECT_FALSE(q.maxSize().has_value());
  EXPECT_TRUE(q.empty());
  EXPECT_EQ(q.itemCount(), 0);
  EXPECT_EQ(q.currentSize(), 0);
  EXPECT_FALSE(q.closed());
  EXPECT_NE(q.capacity(), nullptr);
}

TEST_F(AsyncQueueTest, AsyncQueueCloseIdempotentAndClosedOperations) {
  AsyncQueue<int> q(5);
  EXPECT_TRUE(q.tryPush(1));
  q.close();
  EXPECT_TRUE(q.closed());

  // Second close is a no-op
  q.close();
  EXPECT_TRUE(q.closed());

  // tryPush on closed queue returns false
  EXPECT_FALSE(q.tryPush(2));

  // Items before close can still be popped
  auto item = q.tryPop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 1);

  // tryPop on drained closed queue returns std::nullopt
  EXPECT_FALSE(q.tryPop().has_value());
}

TEST_F(AsyncQueueTest, AsyncQueuePushPopAwaitableOnClosedQueueImmediate) {
  AsyncQueue<int> q(5);
  q.close();

  // push on closed queue fast-fails with FailedPreconditionError
  std::optional<absl::Status> push_result;
  DetachedHandle h1 = launch(
      [](AsyncQueue<int>& queue) -> Task<absl::Status> { co_return co_await queue.push(10); }(q),
      executor_, [&push_result](absl::Status s) { push_result = std::move(s); }, StartMode::Inline);
  ASSERT_TRUE(push_result.has_value());
  EXPECT_TRUE(absl::IsFailedPrecondition(*push_result));

  // pop on empty closed queue immediately returns EOF
  std::optional<absl::StatusOr<std::optional<int>>> pop_result;
  DetachedHandle h2 = launch(
      [](AsyncQueue<int>& queue) -> Task<absl::StatusOr<std::optional<int>>> {
        co_return co_await queue.pop();
      }(q),
      executor_,
      [&pop_result](absl::StatusOr<std::optional<int>> res) { pop_result = std::move(res); },
      StartMode::Inline);
  ASSERT_TRUE(pop_result.has_value());
  ASSERT_TRUE(pop_result->ok());
  EXPECT_FALSE(pop_result->value().has_value());
}

TEST_F(AsyncQueueTest, AsyncQueueMoveOnlyTypes) {
  AsyncQueue<std::unique_ptr<int>> q(2);
  EXPECT_TRUE(q.tryPush(std::make_unique<int>(100)));
  EXPECT_TRUE(q.tryPush(std::make_unique<int>(200)));
  EXPECT_FALSE(q.tryPush(std::make_unique<int>(300)));

  auto item1 = q.tryPop();
  ASSERT_TRUE(item1.has_value());
  ASSERT_NE(*item1, nullptr);
  EXPECT_EQ(**item1, 100);

  auto item2 = q.tryPop();
  ASSERT_TRUE(item2.has_value());
  ASSERT_NE(*item2, nullptr);
  EXPECT_EQ(**item2, 200);
}

} // namespace
} // namespace Coroutine
} // namespace Envoy
