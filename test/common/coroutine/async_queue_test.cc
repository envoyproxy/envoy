#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "source/common/coroutine/async_queue.h"
#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/task.h"

#include "test/common/coroutine/manual_executor.h"
#include "test/test_common/simulated_time_system.h"
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
    ASSIGN_OR_CO_RETURN(auto res, co_await queue.pop());
    if (res.has_value()) {
      if (out_val != nullptr) {
        *out_val = std::move(*res);
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
      ASSIGN_OR_CO_RETURN(auto val_or, co_await queue.pop());
      if (!val_or.has_value()) {
        break;
      }
      if (out_vec != nullptr) {
        out_vec->push_back(std::move(*val_or));
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

TEST_F(AsyncQueueTest, AsyncQueueMoveOnlyTypes) {
  AsyncQueue<std::unique_ptr<int>> queue;

  auto p1 = std::make_unique<int>(100);
  auto p2 = std::make_unique<int>(200);

  EXPECT_TRUE(queue.tryPush(std::move(p1)));
  EXPECT_TRUE(queue.tryPush(std::move(p2)));
  EXPECT_EQ(queue.itemCount(), 2);

  auto r1 = queue.tryPop();
  ASSERT_TRUE(r1.has_value());
  EXPECT_EQ(**r1, 100);

  auto r2 = queue.tryPop();
  ASSERT_TRUE(r2.has_value());
  EXPECT_EQ(**r2, 200);
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
    co_return push_status;
  };

  handles_.push_back(launch(push_task(), executor_, [](absl::Status) {}));
  drain();

  queue.close();
  drain();

  // Pop item 1 to free capacity and unblock push(2), which checks closed_ and returns
  // FailedPrecondition
  auto item = queue.tryPop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 1);
  drain();

  EXPECT_THAT(push_status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
}

TEST_F(AsyncQueueTest, AsyncQueueCloseIdempotent) {
  AsyncQueue<int> queue;
  EXPECT_FALSE(queue.isClosed());
  queue.close();
  EXPECT_TRUE(queue.isClosed());
  // Idempotent second close
  queue.close();
  EXPECT_TRUE(queue.isClosed());
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

  // Push 2 must now be unblocked immediately because push 1 is cancelled
  EXPECT_TRUE(push2_done);
  EXPECT_EQ(q2.itemCount(), 1);
  EXPECT_EQ(shared_cap->currentSize(), 8); // 5 + 3
}

// ============================================================================
// 3. Shared Capacity Across Multiple Queues & Pipelines
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
      ASSIGN_OR_CO_RETURN(auto item_or, co_await q1.pop());
      if (!item_or.has_value()) {
        q2.close();
        break;
      }
      CO_RETURN_IF_ERROR(co_await q2.push(*item_or * 10));
    }
    co_return absl::OkStatus();
  };

  // Filter 2: pops from q2, adds 1, pushes to q3
  auto filter2_task = [&q2, &q3]() -> Task<absl::Status> {
    while (true) {
      ASSIGN_OR_CO_RETURN(auto item_or, co_await q2.pop());
      if (!item_or.has_value()) {
        q3.close();
        break;
      }
      CO_RETURN_IF_ERROR(co_await q3.push(*item_or + 1));
    }
    co_return absl::OkStatus();
  };

  // Sink: pops from q3, collects into sink_received
  auto sink_task = [&q3, &sink_received]() -> Task<absl::Status> {
    while (true) {
      ASSIGN_OR_CO_RETURN(auto item_or, co_await q3.pop());
      if (!item_or.has_value()) {
        break;
      }
      sink_received.push_back(*item_or);
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

  // Pop initial item to unblock first waiter (q1_1)
  EXPECT_TRUE(q1.tryPop().has_value());
  drain();
  EXPECT_THAT(order_granted, testing::ElementsAre("q1_1"));

  // Pop q1_1 -> unblocks q2_1
  EXPECT_TRUE(q1.tryPop().has_value());
  drain();
  EXPECT_THAT(order_granted, testing::ElementsAre("q1_1", "q2_1"));

  // Pop q2_1 -> unblocks q3_1
  EXPECT_TRUE(q2.tryPop().has_value());
  drain();
  EXPECT_THAT(order_granted, testing::ElementsAre("q1_1", "q2_1", "q3_1"));

  // Pop q3_1 -> unblocks q1_2
  EXPECT_TRUE(q3.tryPop().has_value());
  drain();
  EXPECT_THAT(order_granted, testing::ElementsAre("q1_1", "q2_1", "q3_1", "q1_2"));

  // Pop q1_2 -> unblocks q3_2
  EXPECT_TRUE(q1.tryPop().has_value());
  drain();
  EXPECT_THAT(order_granted, testing::ElementsAre("q1_1", "q2_1", "q3_1", "q1_2", "q3_2"));

  // Pop q3_2 -> unblocks q2_2
  EXPECT_TRUE(q3.tryPop().has_value());
  drain();
  EXPECT_THAT(order_granted, testing::ElementsAre("q1_1", "q2_1", "q3_1", "q1_2", "q3_2", "q2_2"));
}

// ============================================================================
// 4. Oversized Items and Anti-Starvation
// ============================================================================

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

  // Now normal chunk is granted
  EXPECT_TRUE(normal_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 10);
}

// ============================================================================
// 5. Direct SharedCapacity Semaphore Semantics
// ============================================================================

TEST_F(AsyncQueueTest, SharedCapacityDirectAcquireRelease) {
  auto cap = std::make_shared<SharedCapacity>(2);
  auto res1 = cap->tryAcquire(2);
  ASSERT_TRUE(res1.has_value());
  EXPECT_TRUE(res1->hasCapacity());
  EXPECT_EQ(res1->size(), 2);
  EXPECT_FALSE(cap->tryAcquire(1).has_value());
  EXPECT_EQ(cap->currentSize(), 2);

  res1->release();
  EXPECT_EQ(cap->currentSize(), 0);
  EXPECT_FALSE(res1->hasCapacity());
  EXPECT_EQ(res1->size(), 0);

  auto res2 = cap->tryAcquire(1);
  ASSERT_TRUE(res2.has_value());
  EXPECT_EQ(cap->currentSize(), 1);

  // Destructor releases capacity
  res2.reset();
  EXPECT_EQ(cap->currentSize(), 0);
}

TEST_F(AsyncQueueTest, SharedCapacityRequestAndCancel) {
  auto cap = std::make_shared<SharedCapacity>(1);
  auto res_hold = cap->tryAcquire(1);
  ASSERT_TRUE(res_hold.has_value());

  bool task1_done = false;
  bool task2_done = false;
  std::optional<absl::Status> task1_status;
  std::optional<CapacityReservation> task2_res;

  auto t1 = [&]() -> Task<absl::Status> {
    auto res_or = co_await cap->acquire(1);
    task1_status = res_or.status();
    task1_done = true;
    co_return *task1_status;
  };
  auto t2 = [&]() -> Task<absl::Status> {
    ASSIGN_OR_CO_RETURN(task2_res, co_await cap->acquire(1));
    task2_done = true;
    co_return absl::OkStatus();
  };

  DetachedHandle h1 = launch(t1(), executor_, [](absl::Status) {});
  DetachedHandle h2 = launch(t2(), executor_, [](absl::Status) {});
  drain();

  EXPECT_FALSE(task1_done);
  EXPECT_FALSE(task2_done);

  // Cancel task1 (at head of capacity wait list)
  h1.cancel();
  drain();

  ASSERT_TRUE(task1_status.has_value());
  EXPECT_TRUE(absl::IsCancelled(*task1_status));

  // Release capacity: task2 (now head) must unblock
  res_hold.reset();
  drain();

  EXPECT_TRUE(task2_done);
  EXPECT_TRUE(task2_res.has_value());
  EXPECT_EQ(cap->currentSize(), 1);
}

TEST_F(AsyncQueueTest, PushOnClosedQueueReturnsError) {
  AsyncQueue<int> queue;
  queue.close();
  EXPECT_FALSE(queue.tryPush(1));

  absl::Status push_status;
  auto push_task = [&queue, &push_status]() -> Task<absl::Status> {
    push_status = co_await queue.push(1);
    co_return push_status;
  };
  handles_.push_back(launch(push_task(), executor_, [](absl::Status) {}));
  drain();
  EXPECT_THAT(push_status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
}

TEST_F(AsyncQueueTest, SharedCapacityReservationReleaseUnblocksAcquireWaiter) {
  auto cap = std::make_shared<SharedCapacity>(1);
  auto res_hold = cap->tryAcquire(1);
  ASSERT_TRUE(res_hold.has_value());

  std::optional<CapacityReservation> waiter_res;
  auto acquire_task = [&cap, &waiter_res]() -> Task<absl::Status> {
    ASSIGN_OR_CO_RETURN(waiter_res, co_await cap->acquire(1));
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(acquire_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  drain();
  EXPECT_FALSE(waiter_res.has_value());
  EXPECT_EQ(cap->currentSize(), 1);

  // Releasing res_hold unblocks the waiting acquire task and grants it the reservation
  res_hold.reset();
  drain();
  ASSERT_TRUE(waiter_res.has_value());
  EXPECT_TRUE(waiter_res->hasCapacity());
  EXPECT_EQ(waiter_res->size(), 1);
  EXPECT_EQ(cap->currentSize(), 1);

  // Dropping waiter_res returns capacity to 0
  waiter_res.reset();
  EXPECT_EQ(cap->currentSize(), 0);
}

TEST_F(AsyncQueueTest, SharedCapacityReleaseZero) {
  auto cap = std::make_shared<SharedCapacity>(10);
  auto res = cap->tryAcquire(5);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(cap->currentSize(), 5);

  // Release 0 is a no-op
  cap->release(0);
  EXPECT_EQ(cap->currentSize(), 5);

  res.reset();
  EXPECT_EQ(cap->currentSize(), 0);
}

// ============================================================================
// 6. any_of Rendezvous, Direct Handoff, and Multi-Stage Pipeline
// ============================================================================

TEST_F(AsyncQueueTest, LateDirectHandoffToLateConsumerViaAnyOf) {
  // Shared capacity limit = 1 item
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  AsyncQueue<std::string> q1(shared_cap);
  AsyncQueue<std::string> q2(shared_cap);

  // Fill capacity with q1
  EXPECT_TRUE(q1.tryPush("item_in_q1"));
  EXPECT_EQ(shared_cap->currentSize(), 1);

  bool q2_push_done = false;
  launchPush(q2, "rendezvous_item", &q2_push_done);
  drain();

  // q2.push is suspended in any_of (racing capacity acquisition vs popper rendezvous)
  EXPECT_FALSE(q2_push_done);
  EXPECT_TRUE(q2.empty());
  EXPECT_EQ(shared_cap->currentSize(), 1);

  // Late consumer arrives and calls q2.pop()
  std::optional<std::string> q2_popped;
  launchPop(q2, q2_popped);
  drain();

  // Item was handed off directly via rendezvous without consuming buffer capacity
  EXPECT_TRUE(q2_push_done);
  ASSERT_TRUE(q2_popped.has_value());
  EXPECT_EQ(*q2_popped, "rendezvous_item");
  EXPECT_EQ(shared_cap->currentSize(), 1); // Capacity still owned by q1

  // Pop q1
  auto q1_popped = q1.tryPop();
  ASSERT_TRUE(q1_popped.has_value());
  EXPECT_EQ(*q1_popped, "item_in_q1");
  EXPECT_EQ(shared_cap->currentSize(), 0);
}

TEST_F(AsyncQueueTest, TryPopNonBlockingWithWaitingPusher) {
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  AsyncQueue<std::string> q1(shared_cap);
  AsyncQueue<std::string> q2(shared_cap);

  // Fill shared capacity
  EXPECT_TRUE(q1.tryPush("q1_held"));
  EXPECT_EQ(shared_cap->currentSize(), 1);

  bool q2_push_done = false;
  launchPush(q2, "rendezvous_for_try_pop", &q2_push_done);
  drain();

  EXPECT_FALSE(q2_push_done);

  // Non-blocking tryPop() should transfer item directly from waiting pusher in any_of
  auto val = q2.tryPop();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, "rendezvous_for_try_pop");

  drain();
  EXPECT_TRUE(q2_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 1);

  // Subsequent tryPop is empty
  EXPECT_FALSE(q2.tryPop().has_value());
}

TEST_F(AsyncQueueTest, MoveOnlyTypesInAnyOfPushPop) {
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  AsyncQueue<std::unique_ptr<std::string>> q1(shared_cap);
  AsyncQueue<std::unique_ptr<std::string>> q2(shared_cap);

  EXPECT_TRUE(q1.tryPush(std::make_unique<std::string>("held_in_q1")));
  EXPECT_EQ(shared_cap->currentSize(), 1);

  bool q2_push_done = false;
  launchPush(q2, std::make_unique<std::string>("move_only_rendezvous"), &q2_push_done);
  drain();

  EXPECT_FALSE(q2_push_done);

  std::optional<std::unique_ptr<std::string>> q2_popped;
  launchPop(q2, q2_popped);
  drain();

  EXPECT_TRUE(q2_push_done);
  ASSERT_TRUE(q2_popped.has_value());
  ASSERT_NE(*q2_popped, nullptr);
  EXPECT_EQ(**q2_popped, "move_only_rendezvous");
  EXPECT_EQ(shared_cap->currentSize(), 1);
}

TEST_F(AsyncQueueTest, AnyOfPushCancellationCleansUpRendezvousAndCapacity) {
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  AsyncQueue<std::string> q1(shared_cap);
  AsyncQueue<std::string> q2(shared_cap);

  EXPECT_TRUE(q1.tryPush("held"));

  std::optional<absl::Status> push_status;
  auto push_task = [&q2, &push_status]() -> Task<absl::Status> {
    push_status = co_await q2.push("cancelled_item");
    co_return *push_status;
  };

  DetachedHandle handle = launch(push_task(), executor_, [](absl::Status) {});
  drain();

  EXPECT_FALSE(push_status.has_value());

  // Cancel the pusher while racing in any_of
  handle.cancel();
  drain();

  ASSERT_TRUE(push_status.has_value());
  EXPECT_TRUE(absl::IsCancelled(*push_status));

  // Queue remains empty, tryPop returns nullopt
  EXPECT_FALSE(q2.tryPop().has_value());

  // Releasing q1 restores capacity to 0 without any capacity leaked
  EXPECT_EQ(q1.tryPop(), "held");
  EXPECT_EQ(shared_cap->currentSize(), 0);
}

TEST_F(AsyncQueueTest, AnyOfPushQueueClosureAbortsWaitingPushers) {
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  AsyncQueue<std::string> q1(shared_cap);
  AsyncQueue<std::string> q2(shared_cap);

  EXPECT_TRUE(q1.tryPush("held"));

  absl::Status push_status;
  auto push_task = [&q2, &push_status]() -> Task<absl::Status> {
    push_status = co_await q2.push("aborted_item");
    co_return push_status;
  };

  handles_.push_back(launch(push_task(), executor_, [](absl::Status) {}));
  drain();

  EXPECT_TRUE(push_status.ok()); // not completed yet

  // Closing q2 must abort the pusher waiting in any_of immediately
  q2.close();
  drain();

  EXPECT_THAT(push_status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
  EXPECT_FALSE(q2.tryPop().has_value());

  EXPECT_EQ(q1.tryPop(), "held");
  EXPECT_EQ(shared_cap->currentSize(), 0);
}

TEST_F(AsyncQueueTest, MultiplePushersInAnyOfRendezvousFIFOHandoff) {
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  AsyncQueue<std::string> q1(shared_cap);
  AsyncQueue<std::string> q2(shared_cap);

  EXPECT_TRUE(q1.tryPush("held"));

  bool push1_done = false;
  bool push2_done = false;
  bool push3_done = false;

  launchPush(q2, "item1", &push1_done);
  launchPush(q2, "item2", &push2_done);
  launchPush(q2, "item3", &push3_done);
  drain();

  EXPECT_FALSE(push1_done);
  EXPECT_FALSE(push2_done);
  EXPECT_FALSE(push3_done);

  // Pop 1: receives item1
  auto p1 = q2.tryPop();
  drain();
  EXPECT_TRUE(push1_done);
  EXPECT_FALSE(push2_done);
  EXPECT_FALSE(push3_done);
  ASSERT_TRUE(p1.has_value());
  EXPECT_EQ(*p1, "item1");

  // Pop 2: receives item2
  auto p2 = q2.tryPop();
  drain();
  EXPECT_TRUE(push2_done);
  EXPECT_FALSE(push3_done);
  ASSERT_TRUE(p2.has_value());
  EXPECT_EQ(*p2, "item2");

  // Pop 3: receives item3
  auto p3 = q2.tryPop();
  drain();
  EXPECT_TRUE(push3_done);
  ASSERT_TRUE(p3.has_value());
  EXPECT_EQ(*p3, "item3");

  EXPECT_FALSE(q2.tryPop().has_value());
  EXPECT_EQ(shared_cap->currentSize(), 1);
  EXPECT_EQ(q1.tryPop(), "held");
  EXPECT_EQ(shared_cap->currentSize(), 0);
}

class AsyncQueuePipelineTest : public testing::Test {
public:
  AsyncQueuePipelineTest()
      : api_(Api::createApiForTest(time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")),
        executor_(std::make_shared<DispatcherExecutor>(*dispatcher_)) {}

  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<DispatcherExecutor> executor_;
  std::vector<DetachedHandle> handles_;
};

TEST_F(AsyncQueuePipelineTest, MultiStageChainedPipelineWithSleepDelaysUnderSharedCapacity) {
  // S1 -> Q1 -> S2 -> Q2 -> S3 -> Sink
  // All 3 queues share SharedCapacity(1).
  // S2 and S3 introduce asynchronous sleep delays, causing frequent backpressure and late consumer
  // arrivals where pushers must rendezvous with poppers via any_of to avoid deadlock.
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  AsyncQueue<int> q1(shared_cap);
  AsyncQueue<int> q2(shared_cap);
  AsyncQueue<int> q3(shared_cap);

  std::vector<int> sink_received;

  // Producer S1: pushes 5 items (0..4) into Q1, then closes Q1
  auto producer_task = [&q1]() -> Task<absl::Status> {
    for (int i = 0; i < 5; ++i) {
      CO_RETURN_IF_ERROR(co_await q1.push(i));
    }
    q1.close();
    co_return absl::OkStatus();
  };

  // Stage S2: pops from Q1, sleeps 10ms, multiplies by 10, pushes to Q2. Closes Q2 on EOF.
  auto stage2_task = [&q1, &q2]() -> Task<absl::Status> {
    while (true) {
      ASSIGN_OR_CO_RETURN(auto item_or, co_await q1.pop());
      if (!item_or.has_value()) {
        q2.close();
        break;
      }
      CO_RETURN_IF_ERROR(co_await sleep(std::chrono::milliseconds(10)));
      CO_RETURN_IF_ERROR(co_await q2.push(*item_or * 10));
    }
    co_return absl::OkStatus();
  };

  // Stage S3: pops from Q2, sleeps 10ms, adds 1, pushes to Q3. Closes Q3 on EOF.
  auto stage3_task = [&q2, &q3]() -> Task<absl::Status> {
    while (true) {
      ASSIGN_OR_CO_RETURN(auto item_or, co_await q2.pop());
      if (!item_or.has_value()) {
        q3.close();
        break;
      }
      CO_RETURN_IF_ERROR(co_await sleep(std::chrono::milliseconds(10)));
      CO_RETURN_IF_ERROR(co_await q3.push(*item_or + 1));
    }
    co_return absl::OkStatus();
  };

  // Sink: pops from Q3, collects items
  auto sink_task = [&q3, &sink_received]() -> Task<absl::Status> {
    while (true) {
      ASSIGN_OR_CO_RETURN(auto item_or, co_await q3.pop());
      if (!item_or.has_value()) {
        break;
      }
      sink_received.push_back(*item_or);
    }
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(producer_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(
      launch(stage2_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(
      launch(stage3_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(
      launch(sink_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));

  // Advance time until all pipeline stages complete
  for (int step = 0; step < 50; ++step) {
    time_system_.advanceTimeAndRun(std::chrono::milliseconds(10), *dispatcher_,
                                   Event::Dispatcher::RunType::NonBlock);
  }

  // 0*10+1=1, 1*10+1=11, 2*10+1=21, 3*10+1=31, 4*10+1=41
  EXPECT_THAT(sink_received, testing::ElementsAre(1, 11, 21, 31, 41));
  EXPECT_EQ(shared_cap->currentSize(), 0);
}

TEST_F(AsyncQueueTest, MultipleWaitingPoppersDirectHandoffWhenCapacityFull) {
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  AsyncQueue<int> q1(shared_cap);
  AsyncQueue<int> q2(shared_cap);

  // Fill capacity with q1
  EXPECT_TRUE(q1.tryPush(999));
  EXPECT_EQ(shared_cap->currentSize(), 1);

  // Start two waiting poppers on q2
  std::optional<int> pop1;
  std::optional<int> pop2;
  launchPop(q2, pop1);
  launchPop(q2, pop2);
  drain();

  EXPECT_FALSE(pop1.has_value());
  EXPECT_FALSE(pop2.has_value());

  // Push two items to q2 in the same turn while capacity is full
  bool push1_done = false;
  bool push2_done = false;
  launchPush(q2, 10, &push1_done);
  launchPush(q2, 20, &push2_done);
  drain();

  // Both should be handed off directly to waiting poppers
  EXPECT_TRUE(push1_done);
  EXPECT_TRUE(push2_done);
  ASSERT_TRUE(pop1.has_value());
  ASSERT_TRUE(pop2.has_value());
  EXPECT_EQ(*pop1, 10);
  EXPECT_EQ(*pop2, 20);
}

TEST_F(AsyncQueueTest, MultipleWaitingPoppersDirectHandoffTryPush) {
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  AsyncQueue<int> q1(shared_cap);
  AsyncQueue<int> q2(shared_cap);

  EXPECT_TRUE(q1.tryPush(999));
  EXPECT_EQ(shared_cap->currentSize(), 1);

  std::optional<int> pop1;
  std::optional<int> pop2;
  launchPop(q2, pop1);
  launchPop(q2, pop2);
  drain();

  EXPECT_TRUE(q2.tryPush(10));
  EXPECT_TRUE(q2.tryPush(20));
  EXPECT_FALSE(q2.tryPush(30)); // No more waiting poppers, capacity full -> returns false!

  drain();

  ASSERT_TRUE(pop1.has_value());
  ASSERT_TRUE(pop2.has_value());
  EXPECT_EQ(*pop1, 10);
  EXPECT_EQ(*pop2, 20);
  EXPECT_EQ(shared_cap->currentSize(), 1);
}

TEST_F(AsyncQueueTest, CancelPusherInMiddleOfPusherWaitersFIFO) {
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  AsyncQueue<int> q1(shared_cap);
  AsyncQueue<int> q2(shared_cap);

  EXPECT_TRUE(q1.tryPush(999));

  std::optional<absl::Status> push1_status;
  std::optional<absl::Status> push2_status;
  std::optional<absl::Status> push3_status;

  auto p1 = [&]() -> Task<absl::Status> {
    push1_status = co_await q2.push(1);
    co_return *push1_status;
  };
  auto p2 = [&]() -> Task<absl::Status> {
    push2_status = co_await q2.push(2);
    co_return *push2_status;
  };
  auto p3 = [&]() -> Task<absl::Status> {
    push3_status = co_await q2.push(3);
    co_return *push3_status;
  };

  DetachedHandle h1 = launch(p1(), executor_, [](absl::Status) {});
  DetachedHandle h2 = launch(p2(), executor_, [](absl::Status) {});
  DetachedHandle h3 = launch(p3(), executor_, [](absl::Status) {});
  drain();

  // Cancel p2 (middle waiter)
  h2.cancel();
  drain();
  ASSERT_TRUE(push2_status.has_value());
  EXPECT_TRUE(absl::IsCancelled(*push2_status));

  // Pop from q2 -> should receive 1
  auto r1 = q2.tryPop();
  ASSERT_TRUE(r1.has_value());
  EXPECT_EQ(*r1, 1);
  drain();
  ASSERT_TRUE(push1_status.has_value());
  EXPECT_TRUE(push1_status->ok());

  // Pop from q2 -> should skip p2 (cancelled) and receive 3
  auto r2 = q2.tryPop();
  ASSERT_TRUE(r2.has_value());
  EXPECT_EQ(*r2, 3);
  drain();
  ASSERT_TRUE(push3_status.has_value());
  EXPECT_TRUE(push3_status->ok());

  EXPECT_FALSE(q2.tryPop().has_value());
}

TEST_F(AsyncQueueTest, CancelHeadPusherInPusherWaitersFIFO) {
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  AsyncQueue<int> q1(shared_cap);
  AsyncQueue<int> q2(shared_cap);

  EXPECT_TRUE(q1.tryPush(999));

  std::optional<absl::Status> push1_status;
  std::optional<absl::Status> push2_status;

  auto p1 = [&]() -> Task<absl::Status> {
    push1_status = co_await q2.push(1);
    co_return *push1_status;
  };
  auto p2 = [&]() -> Task<absl::Status> {
    push2_status = co_await q2.push(2);
    co_return *push2_status;
  };

  DetachedHandle h1 = launch(p1(), executor_, [](absl::Status) {});
  DetachedHandle h2 = launch(p2(), executor_, [](absl::Status) {});
  drain();

  // Cancel p1 (head waiter)
  h1.cancel();
  drain();
  ASSERT_TRUE(push1_status.has_value());
  EXPECT_TRUE(absl::IsCancelled(*push1_status));

  // Async pop from q2 -> should skip p1 (cancelled) and receive 2
  std::optional<int> pop2_val;
  launchPop(q2, pop2_val);
  drain();

  ASSERT_TRUE(pop2_val.has_value());
  EXPECT_EQ(*pop2_val, 2);
  ASSERT_TRUE(push2_status.has_value());
  EXPECT_TRUE(push2_status->ok());
}

TEST_F(AsyncQueueTest, AsyncQueueDestructionWithPusherInAnyOf) {
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  auto q1 = std::make_unique<AsyncQueue<int>>(shared_cap);
  auto q2 = std::make_unique<AsyncQueue<int>>(shared_cap);

  EXPECT_TRUE(q1->tryPush(999));

  std::optional<absl::Status> push_status;
  auto push_task = [&q2, &push_status]() -> Task<absl::Status> {
    push_status = co_await q2->push(42);
    co_return *push_status;
  };

  handles_.push_back(launch(push_task(), executor_, [](absl::Status) {}));
  drain();

  EXPECT_FALSE(push_status.has_value());

  // Destroy q2 while pusher is suspended in any_of
  q2.reset();
  drain();

  ASSERT_TRUE(push_status.has_value());
  EXPECT_THAT(*push_status, HasStatusCode(absl::StatusCode::kFailedPrecondition));

  // Capacity was not leaked
  EXPECT_EQ(shared_cap->currentSize(), 1);
  EXPECT_EQ(q1->tryPop(), 999);
  EXPECT_EQ(shared_cap->currentSize(), 0);
}

TEST_F(AsyncQueueTest, SharedCapacityAnyOfAcquireOneBranchWinsAndOneCancels) {
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  auto res_hold = shared_cap->tryAcquire(1);
  ASSERT_TRUE(res_hold.has_value());

  std::optional<absl::StatusOr<absl::variant<CapacityReservation, CapacityReservation>>>
      any_of_result;
  auto acquire_task = [&shared_cap, &any_of_result]() -> Task<absl::Status> {
    any_of_result = co_await any_of(shared_cap->acquire(1), shared_cap->acquire(1));
    co_return any_of_result->status();
  };

  handles_.push_back(
      launch(acquire_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  drain();

  EXPECT_FALSE(any_of_result.has_value());
  EXPECT_EQ(shared_cap->currentSize(), 1);

  // Releasing res_hold unblocks one of the branches
  res_hold.reset();
  drain();

  ASSERT_TRUE(any_of_result.has_value());
  EXPECT_TRUE(any_of_result->ok());
  EXPECT_EQ(shared_cap->currentSize(), 1);

  // Releasing result returns capacity to 0
  any_of_result.reset();
  EXPECT_EQ(shared_cap->currentSize(), 0);
}

// ============================================================================
// 7. CapacityReservation RAII and QueuedItem Reservation Tests
// ============================================================================

TEST_F(AsyncQueueTest, CapacityReservationRAIILifecycleAndMoveSemantics) {
  auto cap = std::make_shared<SharedCapacity>(10);

  // 1. Default constructor
  CapacityReservation default_res;
  EXPECT_EQ(default_res.size(), 0);
  EXPECT_FALSE(default_res.hasCapacity());

  // 2. Acquisition via tryAcquire
  auto res1 = cap->tryAcquire(5);
  ASSERT_TRUE(res1.has_value());
  EXPECT_EQ(res1->size(), 5);
  EXPECT_TRUE(res1->hasCapacity());
  EXPECT_EQ(cap->currentSize(), 5);

  // 3. Move construction
  CapacityReservation res2(std::move(*res1));
  EXPECT_EQ(res1->size(), 0);
  EXPECT_FALSE(res1->hasCapacity());
  EXPECT_EQ(res2.size(), 5);
  EXPECT_TRUE(res2.hasCapacity());
  EXPECT_EQ(cap->currentSize(), 5);

  // 4. Move assignment to empty
  CapacityReservation res3;
  res3 = std::move(res2);
  EXPECT_EQ(res2.size(), 0);
  EXPECT_FALSE(res2.hasCapacity());
  EXPECT_EQ(res3.size(), 5);
  EXPECT_TRUE(res3.hasCapacity());
  EXPECT_EQ(cap->currentSize(), 5);

  // 5. Move assignment overwriting an existing reservation
  auto res4 = cap->tryAcquire(3);
  ASSERT_TRUE(res4.has_value());
  EXPECT_EQ(cap->currentSize(), 8);

  // Overwriting res3 (holding 5) with res4 (holding 3) releases 5 and adopts 3 -> currentSize = 3
  res3 = std::move(*res4);
  EXPECT_EQ(cap->currentSize(), 3);
  EXPECT_EQ(res3.size(), 3);
  EXPECT_TRUE(res3.hasCapacity());
  EXPECT_EQ(res4->size(), 0);
  EXPECT_FALSE(res4->hasCapacity());

  // 6. Self-move assignment safety
  auto res5 = cap->tryAcquire(4);
  ASSERT_TRUE(res5.has_value());
  EXPECT_EQ(cap->currentSize(), 7);
  // Suppress self-move warning for testing
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
  *res5 = std::move(*res5);
#pragma clang diagnostic pop
  EXPECT_EQ(cap->currentSize(), 7);
  EXPECT_EQ(res5->size(), 4);
  EXPECT_TRUE(res5->hasCapacity());

  // 7. Explicit release()
  res3.release();
  EXPECT_EQ(cap->currentSize(), 4);
  res5->release();
  EXPECT_EQ(cap->currentSize(), 0);
  EXPECT_EQ(res5->size(), 0);
  EXPECT_FALSE(res5->hasCapacity());

  // Calling release() again is safe and idempotent
  res5->release();
  EXPECT_EQ(cap->currentSize(), 0);

  // 8. Destructor RAII release
  {
    auto scoped_res = cap->tryAcquire(7);
    ASSERT_TRUE(scoped_res.has_value());
    EXPECT_EQ(cap->currentSize(), 7);
  }
  EXPECT_EQ(cap->currentSize(), 0);

  // 9. Container storage and relocation (vector reallocation)
  {
    std::vector<CapacityReservation> res_vec;
    for (int i = 0; i < 5; ++i) {
      auto r = cap->tryAcquire(2);
      ASSERT_TRUE(r.has_value());
      res_vec.push_back(std::move(*r));
    }
    EXPECT_EQ(cap->currentSize(), 10);
    // Erase one element from the middle
    res_vec.erase(res_vec.begin() + 2);
    EXPECT_EQ(cap->currentSize(), 8);
  }
  EXPECT_EQ(cap->currentSize(), 0);
}

TEST_F(AsyncQueueTest, QueuedItemRaiiCapacityReleaseOnPopAndTryPop) {
  auto shared_cap = std::make_shared<SharedCapacity>(100);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q(shared_cap);

  EXPECT_TRUE(q.tryPush(TestByteItem{std::string(30, 'a')}));
  EXPECT_TRUE(q.tryPush(TestByteItem{std::string(40, 'b')}));
  EXPECT_TRUE(q.tryPush(TestByteItem{std::string(20, 'c')}));

  EXPECT_EQ(shared_cap->currentSize(), 90);
  EXPECT_EQ(q.currentSize(), 90);
  EXPECT_EQ(q.itemCount(), 3);

  // 1. Pop via tryPop()
  auto item1 = q.tryPop();
  ASSERT_TRUE(item1.has_value());
  EXPECT_EQ(item1->data.size(), 30);
  EXPECT_EQ(shared_cap->currentSize(), 60);
  EXPECT_EQ(q.currentSize(), 60);
  EXPECT_EQ(q.itemCount(), 2);

  // 2. Pop via async pop()
  std::optional<TestByteItem> item2;
  launchPop(q, item2);
  drain();

  ASSERT_TRUE(item2.has_value());
  EXPECT_EQ(item2->data.size(), 40);
  EXPECT_EQ(shared_cap->currentSize(), 20);
  EXPECT_EQ(q.currentSize(), 20);
  EXPECT_EQ(q.itemCount(), 1);

  // 3. Pop final item via tryPop()
  auto item3 = q.tryPop();
  ASSERT_TRUE(item3.has_value());
  EXPECT_EQ(item3->data.size(), 20);
  EXPECT_EQ(shared_cap->currentSize(), 0);
  EXPECT_EQ(q.currentSize(), 0);
  EXPECT_EQ(q.itemCount(), 0);
  EXPECT_TRUE(q.empty());
}

TEST_F(AsyncQueueTest, QueuedItemRaiiCapacityReleaseOnQueueDestructionAndClear) {
  auto shared_cap = std::make_shared<SharedCapacity>(100);
  {
    auto q = std::make_unique<AsyncQueue<TestByteItem, TestByteSizeFunc>>(shared_cap);
    EXPECT_TRUE(q->tryPush(TestByteItem{std::string(40, 'a')}));
    EXPECT_TRUE(q->tryPush(TestByteItem{std::string(35, 'b')}));
    EXPECT_EQ(shared_cap->currentSize(), 75);
    EXPECT_EQ(q->currentSize(), 75);
    EXPECT_EQ(q->itemCount(), 2);
  }
  // Queue destroyed with items still inside -> CapacityReservation destructors release capacity
  EXPECT_EQ(shared_cap->currentSize(), 0);
}

TEST_F(AsyncQueueTest, QueuedItemRaiiCapacityReleaseOnQueueCloseAndDrain) {
  auto shared_cap = std::make_shared<SharedCapacity>(100);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q(shared_cap);

  EXPECT_TRUE(q.tryPush(TestByteItem{std::string(40, 'a')}));
  EXPECT_TRUE(q.tryPush(TestByteItem{std::string(30, 'b')}));
  EXPECT_EQ(shared_cap->currentSize(), 70);

  q.close();
  EXPECT_TRUE(q.isClosed());
  // Closing the queue preserves queued items and their held reservations
  EXPECT_EQ(shared_cap->currentSize(), 70);
  EXPECT_EQ(q.itemCount(), 2);

  auto pop1 = q.tryPop();
  ASSERT_TRUE(pop1.has_value());
  EXPECT_EQ(pop1->data.size(), 40);
  EXPECT_EQ(shared_cap->currentSize(), 30);

  auto pop2 = q.tryPop();
  ASSERT_TRUE(pop2.has_value());
  EXPECT_EQ(pop2->data.size(), 30);
  EXPECT_EQ(shared_cap->currentSize(), 0);
  EXPECT_TRUE(q.empty());
}

TEST_F(AsyncQueueTest, AnyOfBranch1WinnerReleasesConcurrentAcquiredReservationWithoutLeak) {
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  AsyncQueue<std::string> q1(shared_cap);
  AsyncQueue<std::string> q2(shared_cap);

  // Fill capacity with q1
  EXPECT_TRUE(q1.tryPush("held_in_q1"));
  EXPECT_EQ(shared_cap->currentSize(), 1);

  bool push_done = false;
  launchPush(q2, "raced_item", &push_done);
  drain();

  EXPECT_FALSE(push_done);
  EXPECT_EQ(shared_cap->currentSize(), 1);

  // Popper arrives on q2 -> direct handoff completes via Branch 1 (rendezvous)
  auto val = q2.tryPop();
  drain();

  EXPECT_TRUE(push_done);
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, "raced_item");
  EXPECT_EQ(shared_cap->currentSize(), 1);

  // Pop q1
  EXPECT_EQ(q1.tryPop(), "held_in_q1");
  EXPECT_EQ(shared_cap->currentSize(), 0);
}

} // namespace
} // namespace Coroutine
} // namespace Envoy
