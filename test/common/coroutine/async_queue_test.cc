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

  // Pushes 1 and 2 completed; 3 is waiting in queue_ pending capacity
  EXPECT_EQ(queue.itemCount(), 3);
  EXPECT_EQ(pushed.size(), 3);

  // Pop one item, which frees space and unblocks push of 3
  std::optional<int> pop1;
  launchPop(queue, pop1);
  drain();
  EXPECT_EQ(pop1, 1);

  // Now push of 3 has completed, and push of 4 suspended in queue_ (items: 2, 3 committed + 4
  // pending)
  EXPECT_EQ(queue.itemCount(), 3);
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
  // 50 bytes -> 60 + 50 = 110 > 100 bytes, suspends in queue_
  launchPush(queue, TestByteItem{std::string(50, 'b')}, &push2_done);
  drain();

  EXPECT_TRUE(push1_done);
  EXPECT_FALSE(push2_done);
  // Both items (60 committed + 50 pending) are in queue_ and accounted in currentSize and itemCount
  EXPECT_EQ(queue.currentSize(), 110);
  EXPECT_EQ(queue.itemCount(), 2);

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

TEST_F(AsyncQueueTest, SuspendedPusherFailsWhenQueueIsClosed) {
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
  EXPECT_FALSE(queue.closed());
  queue.close();
  EXPECT_TRUE(queue.closed());
  // Idempotent second close
  queue.close();
  EXPECT_TRUE(queue.closed());
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
  auto shared_cap = std::make_shared<Capacity>(10);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q1(shared_cap);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q2(shared_cap);

  // Fill 5 units in q1 -> shared_cap has 5 units free
  EXPECT_TRUE(q1.tryPush(TestByteItem{std::string(5, 'a')}));
  EXPECT_EQ(shared_cap->currentPermits(), 5);

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
  EXPECT_EQ(shared_cap->currentPermits(), 8); // 5 + 3
}

// ============================================================================
// 3. Shared Capacity Across Multiple Queues & Pipelines
// ============================================================================

TEST_F(AsyncQueueTest, CapacityAcrossMultipleQueues) {
  auto shared_cap = std::make_shared<Capacity>(3);
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
  EXPECT_EQ(shared_cap->currentPermits(), 3);
  EXPECT_EQ(q1.currentSize(), 2);
  EXPECT_EQ(q2.currentSize(), 1);

  // Pushing into q2 should suspend
  bool q2_push_done = false;
  launchPush(q2, 40, &q2_push_done);
  drain();

  EXPECT_FALSE(q2_push_done);
  EXPECT_EQ(shared_cap->currentPermits(), 3);

  // Pop from q1, which frees 1 slot in shared capacity and unblocks q2's push
  auto pop1 = q1.tryPop();
  ASSERT_TRUE(pop1.has_value());
  EXPECT_EQ(*pop1, 10);

  drain();
  EXPECT_TRUE(q2_push_done);
  EXPECT_EQ(shared_cap->currentPermits(), 3);
  EXPECT_EQ(q1.currentSize(), 1);
  EXPECT_EQ(q2.currentSize(), 2);
}

TEST_F(AsyncQueueTest, CapacityByteBudgetAcrossChainedQueues) {
  auto shared_cap = std::make_shared<Capacity>(100);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q1(shared_cap);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q2(shared_cap);

  EXPECT_TRUE(q1.tryPush(TestByteItem{std::string(60, 'a')}));
  EXPECT_TRUE(q2.tryPush(TestByteItem{std::string(40, 'b')}));
  EXPECT_EQ(shared_cap->currentPermits(), 100);

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
  EXPECT_EQ(shared_cap->currentPermits(), 90); // 60 + 30
}

TEST_F(AsyncQueueTest, CapacityQueueDestructionReleasesCapacity) {
  auto shared_cap = std::make_shared<Capacity>(2);
  auto q1 = std::make_unique<AsyncQueue<int>>(shared_cap);
  AsyncQueue<int> q2(shared_cap);

  EXPECT_TRUE(q1->tryPush(1));
  EXPECT_TRUE(q1->tryPush(2));
  EXPECT_EQ(shared_cap->currentPermits(), 2);

  bool q2_push_done = false;
  launchPush(q2, 3, &q2_push_done);
  drain();
  EXPECT_FALSE(q2_push_done);

  // Destroying q1 releases its 2 units from shared capacity
  q1.reset();

  drain();
  EXPECT_TRUE(q2_push_done);
  EXPECT_EQ(shared_cap->currentPermits(), 1);
}

TEST_F(AsyncQueueTest, DirectHandoffBypassesCapacityWhenFull) {
  auto shared_cap = std::make_shared<Capacity>(1);
  AsyncQueue<int> q1(shared_cap);
  AsyncQueue<int> q2(shared_cap);

  // Fill shared capacity using q1
  EXPECT_TRUE(q1.tryPush(100));
  EXPECT_EQ(shared_cap->currentPermits(), 1);

  // Start waiting popper on q2
  std::optional<int> q2_popped;
  launchPop(q2, q2_popped);
  drain();
  EXPECT_FALSE(q2_popped.has_value());

  // q2.tryPush should succeed via direct handoff even though Capacity is full
  EXPECT_TRUE(q2.tryPush(200));
  EXPECT_TRUE(q2_popped.has_value());
  EXPECT_EQ(*q2_popped, 200);
  EXPECT_EQ(shared_cap->currentPermits(), 1);
  EXPECT_TRUE(q2.empty());
}

TEST_F(AsyncQueueTest, ChainedQueuesPipelineStreamingUnderCapacityConstraint) {
  // 3-stage pipeline sharing 1 capacity unit: Q1 -> F1 -> Q2 -> F2 -> Q3 -> Sink
  auto shared_cap = std::make_shared<Capacity>(1);
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
  auto shared_cap = std::make_shared<Capacity>(1);
  AsyncQueue<std::string> q1(shared_cap);
  AsyncQueue<std::string> q2(shared_cap);
  AsyncQueue<std::string> q3(shared_cap);

  // Initial fill
  EXPECT_TRUE(q1.tryPush("init"));
  EXPECT_EQ(shared_cap->currentPermits(), 1);

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
  auto shared_cap = std::make_shared<Capacity>(100);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q1(shared_cap);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q2(shared_cap);

  // Fill 60 bytes in q1 (40 bytes free in shared_cap)
  EXPECT_TRUE(q1.tryPush(TestByteItem{std::string(60, 'a')}));
  EXPECT_EQ(shared_cap->currentPermits(), 60);

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
  EXPECT_EQ(shared_cap->currentPermits(), 60);

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
  EXPECT_EQ(shared_cap->currentPermits(), 100);
  EXPECT_EQ(q1.itemCount(), 1);
  EXPECT_EQ(q2.itemCount(), 1);
}

TEST_F(AsyncQueueTest, OversizedChunkAdmissionAfterDrain) {
  // Shared capacity limit = 50 bytes
  auto shared_cap = std::make_shared<Capacity>(50);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q1(shared_cap);
  AsyncQueue<TestByteItem, TestByteSizeFunc> q2(shared_cap);

  // q1 holds 30 bytes
  EXPECT_TRUE(q1.tryPush(TestByteItem{std::string(30, 'a')}));
  EXPECT_EQ(shared_cap->currentPermits(), 30);

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
  EXPECT_EQ(shared_cap->currentPermits(), 100);

  // Pop the oversized chunk from q2 -> current_size becomes 0
  auto item2 = q2.tryPop();
  ASSERT_TRUE(item2.has_value());
  EXPECT_EQ(item2->data.size(), 100);

  drain();

  // Now normal chunk is granted
  EXPECT_TRUE(normal_push_done);
  EXPECT_EQ(shared_cap->currentPermits(), 10);
}

// ============================================================================
// 5. Direct Capacity Semaphore Semantics
// ============================================================================

TEST_F(AsyncQueueTest, CapacityDirectAcquireRelease) {
  auto cap = std::make_shared<Capacity>(2);
  auto res1 = cap->tryAcquire(2);
  EXPECT_TRUE(res1.has_value());
  EXPECT_FALSE(cap->tryAcquire(1).has_value());
  EXPECT_EQ(cap->currentPermits(), 2);

  res1->release();
  EXPECT_EQ(cap->currentPermits(), 0);

  auto res2 = cap->tryAcquire(1);
  EXPECT_TRUE(res2.has_value());
  EXPECT_EQ(cap->currentPermits(), 1);

  res2->release();
  EXPECT_EQ(cap->currentPermits(), 0);
}

TEST_F(AsyncQueueTest, CapacityRequestAndCancel) {
  auto cap = std::make_shared<Capacity>(1);
  auto init_res = cap->tryAcquire(1);
  EXPECT_TRUE(init_res.has_value());

  bool task1_done = false;
  bool task2_done = false;
  std::optional<absl::Status> task1_status;

  auto t1 = [&]() -> Task<absl::Status> {
    auto res = co_await cap->acquire(1);
    task1_status = res.status();
    task1_done = true;
    co_return *task1_status;
  };
  std::optional<CapacityReservation> task2_res;
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
  init_res->release();
  drain();

  EXPECT_TRUE(task2_done);
  EXPECT_EQ(cap->currentPermits(), 1);
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

TEST_F(AsyncQueueTest, CapacityDestructionAbortsAcquireWaiters) {
  auto cap = std::make_shared<Capacity>(1);
  auto init_res = cap->tryAcquire(1);
  EXPECT_TRUE(init_res.has_value());

  absl::Status waiter_status;
  auto acquire_task = [&cap, &waiter_status]() -> Task<absl::Status> {
    auto res = co_await cap->acquire(1);
    waiter_status = res.status();
    co_return waiter_status;
  };

  handles_.push_back(launch(acquire_task(), executor_, [](absl::Status) {}));
  drain();
  EXPECT_TRUE(waiter_status.ok());

  // Destroy cap while waiter is pending
  cap.reset();
  drain();
  EXPECT_THAT(waiter_status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
}

TEST_F(AsyncQueueTest, CapacityReleaseZero) {
  auto cap = std::make_shared<Capacity>(10);
  auto zero_res = cap->tryAcquire(0);
  ASSERT_TRUE(zero_res.has_value());
  EXPECT_EQ(cap->currentPermits(), 0);
  zero_res->release();
  EXPECT_EQ(cap->currentPermits(), 0);

  auto res = cap->tryAcquire(5);
  EXPECT_TRUE(res.has_value());
  EXPECT_EQ(cap->currentPermits(), 5);

  res->release();
  EXPECT_EQ(cap->currentPermits(), 0);
}

// ============================================================================
// 6. Ownership, PushAccessor, Move Semantics, and Destruction Safety
// ============================================================================

TEST_F(AsyncQueueTest, MovedFromQueueIsInert) {
  AsyncQueue<std::string> q_src(10);
  AsyncQueue<std::string> queue = std::move(q_src); // q_src is now moved-from, core_ == nullptr

  EXPECT_TRUE(q_src.empty());
  EXPECT_TRUE(q_src.closed());
  EXPECT_EQ(q_src.itemCount(), 0);
  EXPECT_EQ(q_src.currentSize(), 0);
  EXPECT_EQ(q_src.maxSize(), std::nullopt);
  EXPECT_EQ(q_src.capacity(), nullptr);

  // tryPush fails on moved-from queue
  EXPECT_FALSE(q_src.tryPush("hello"));

  // tryPop returns nullopt on moved-from queue
  EXPECT_FALSE(q_src.tryPop().has_value());

  // pop() returns immediate EOF on moved-from queue
  bool eof_seen = false;
  launchPop(q_src, nullptr, &eof_seen);
  drain();
  EXPECT_TRUE(eof_seen);

  // push() returns FailedPreconditionError on moved-from queue
  absl::Status push_status;
  auto push_task = [&q_src, &push_status]() -> Task<absl::Status> {
    push_status = co_await q_src.push("hello");
    co_return push_status;
  };
  handles_.push_back(launch(push_task(), executor_, [](absl::Status) {}));
  drain();
  EXPECT_THAT(push_status, HasStatusCode(absl::StatusCode::kFailedPrecondition));

  // close() is a safe no-op on moved-from queue
  q_src.close();
  EXPECT_TRUE(q_src.closed());

  // pushAccessor returns closed accessor
  auto pusher = q_src.pushAccessor();
  EXPECT_TRUE(pusher.closed());
  EXPECT_FALSE(pusher.tryPush("hello"));
}

TEST_F(AsyncQueueTest, MoveConstructionTransfersCoreAndLeavesSourceInert) {
  AsyncQueue<std::string> q1(2);
  EXPECT_TRUE(q1.tryPush("item1"));
  auto p1 = q1.pushAccessor();

  // Move-construct q2 from q1
  AsyncQueue<std::string> q2 = std::move(q1);

  // q1 is now inert
  EXPECT_TRUE(q1.empty());
  EXPECT_TRUE(q1.closed());
  EXPECT_FALSE(q1.tryPush("item_fail"));
  EXPECT_FALSE(q1.tryPop().has_value());

  // q2 has the item and accepts new items
  EXPECT_EQ(q2.itemCount(), 1);
  EXPECT_TRUE(q2.tryPush("item2"));
  EXPECT_EQ(q2.itemCount(), 2);

  // Existing PushAccessor created on q1 pushes into q2's core
  EXPECT_FALSE(p1.tryPush("item3")); // queue is full (capacity 2)

  auto pop1 = q2.tryPop();
  ASSERT_TRUE(pop1.has_value());
  EXPECT_EQ(*pop1, "item1");

  auto pop2 = q2.tryPop();
  ASSERT_TRUE(pop2.has_value());
  EXPECT_EQ(*pop2, "item2");
}

TEST_F(AsyncQueueTest, MoveAssignmentClosesPreviousCoreAndAdoptsNew) {
  AsyncQueue<int> q1(2);
  EXPECT_TRUE(q1.tryPush(42));

  AsyncQueue<int> q2(2);
  EXPECT_TRUE(q2.tryPush(99));

  bool old_q2_eof = false;
  launchPop(q2, nullptr, &old_q2_eof);
  // Pop item 99 from q2, so popper is now suspended on q2 waiting for next item
  auto pop_old = q2.tryPop();
  ASSERT_TRUE(pop_old.has_value());
  EXPECT_EQ(*pop_old, 99);
  drain();
  EXPECT_FALSE(old_q2_eof);

  // Move-assign q1 into q2: this closes old q2's core and unblocks old_q2_eof!
  q2 = std::move(q1);
  drain();
  EXPECT_TRUE(old_q2_eof);

  // q1 is now inert
  EXPECT_TRUE(q1.closed());
  EXPECT_TRUE(q1.empty());

  // q2 now contains 42 from q1
  auto pop_new = q2.tryPop();
  ASSERT_TRUE(pop_new.has_value());
  EXPECT_EQ(*pop_new, 42);
}

TEST_F(AsyncQueueTest, PushAccessorBasicPushAndPop) {
  AsyncQueue<std::string> queue(2);
  auto pusher = queue.pushAccessor();

  EXPECT_FALSE(pusher.closed());
  EXPECT_TRUE(pusher.empty());
  EXPECT_EQ(pusher.currentSize(), 0);

  EXPECT_TRUE(pusher.tryPush("item1"));
  EXPECT_EQ(queue.itemCount(), 1);
  EXPECT_EQ(pusher.itemCount(), 1);

  bool push2_done = false;
  auto push_task = [&pusher, &push2_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await pusher.push("item2"));
    push2_done = true;
    co_return absl::OkStatus();
  };
  handles_.push_back(
      launch(push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  drain();
  EXPECT_TRUE(push2_done);

  // Pop from the owner (queue)
  std::vector<std::string> popped;
  launchPopMultiple(queue, 2, popped);
  drain();
  EXPECT_THAT(popped, testing::ElementsAre("item1", "item2"));
}

TEST_F(AsyncQueueTest, PushAccessorDestructionSafety) {
  auto queue = std::make_unique<AsyncQueue<int>>(2);
  auto pusher = queue->pushAccessor();

  EXPECT_TRUE(pusher.tryPush(10));
  EXPECT_FALSE(pusher.closed());

  // Destroy the owner queue
  queue.reset();

  // PushAccessor should now detect that the underlying queue is destroyed
  EXPECT_TRUE(pusher.closed());
  EXPECT_FALSE(pusher.tryPush(20));

  absl::Status push_status;
  auto push_task = [&pusher, &push_status]() -> Task<absl::Status> {
    push_status = co_await pusher.push(30);
    co_return push_status;
  };
  handles_.push_back(launch(push_task(), executor_, [](absl::Status) {}));
  drain();
  EXPECT_THAT(push_status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
}

TEST_F(AsyncQueueTest, PushAccessorSuspendedPushDoesNotPreventCoreDestruction) {
  auto cap = std::make_shared<Capacity>(1);
  auto queue = std::make_unique<AsyncQueue<int>>(cap);
  auto pusher = queue->pushAccessor();

  // Hold capacity
  auto hold = cap->tryAcquire(1);
  ASSERT_TRUE(hold.has_value());

  absl::Status push_status;
  auto push_task = [&pusher, &push_status]() -> Task<absl::Status> {
    push_status = co_await pusher.push(42);
    co_return push_status;
  };
  handles_.push_back(launch(push_task(), executor_, [](absl::Status) {}));
  drain();

  // Pusher is suspended waiting for capacity
  EXPECT_FALSE(pusher.closed());

  // Destroy the owner queue while pusher is suspended on capacity
  queue.reset();

  // The underlying Core must be immediately destroyed (weak_ptr expired)
  EXPECT_TRUE(pusher.closed());

  // Release capacity so pusher can resume
  hold->release();
  drain();

  // Pusher wakes up, observes Core is destroyed, and returns FailedPreconditionError
  EXPECT_THAT(push_status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
}

TEST_F(AsyncQueueTest, DirectHandoffConsumerDestroysQueue) {
  auto consumer_task = [](std::unique_ptr<AsyncQueue<int>>& q, int expected) -> Task<absl::Status> {
    auto res = co_await q->pop();
    EXPECT_TRUE(res.ok());
    EXPECT_EQ(*res.value(), expected);
    // Destroy the owner queue inside the callback
    q.reset();
    co_return absl::OkStatus();
  };

  // Case 1: PushAccessor::tryPush triggers direct handoff and consumer destroys queue
  {
    auto queue = std::make_unique<AsyncQueue<int>>(10);
    auto pusher = queue->pushAccessor();
    launchTaskOk(consumer_task(queue, 42));
    drain();
    EXPECT_TRUE(pusher.tryPush(42));
    EXPECT_EQ(queue, nullptr);
    EXPECT_TRUE(pusher.closed());
  }

  // Case 2: AsyncQueue::tryPush triggers direct handoff and consumer destroys queue
  {
    auto queue = std::make_unique<AsyncQueue<int>>(10);
    launchTaskOk(consumer_task(queue, 99));
    drain();
    EXPECT_TRUE(queue->tryPush(99));
    EXPECT_EQ(queue, nullptr);
  }
}

TEST_F(AsyncQueueTest, MultiplePushersSinglePopper) {
  // Multiple producers, one consumer (the owner queue)
  AsyncQueue<std::string> queue(10);
  auto p1 = queue.pushAccessor();
  auto p2 = queue.pushAccessor();
  auto p3 = queue.pushAccessor();

  EXPECT_TRUE(p1.tryPush("p1_msg"));
  EXPECT_TRUE(p2.tryPush("p2_msg"));
  EXPECT_TRUE(p3.tryPush("p3_msg"));
  EXPECT_EQ(queue.itemCount(), 3);

  std::vector<std::string> received;
  launchPopMultiple(queue, 3, received);
  drain();
  EXPECT_THAT(received, testing::ElementsAre("p1_msg", "p2_msg", "p3_msg"));
}

TEST_F(AsyncQueueTest, PushAccessorClose) {
  AsyncQueue<int> queue;
  auto pusher = queue.pushAccessor();

  EXPECT_TRUE(pusher.tryPush(1));
  pusher.close();

  EXPECT_TRUE(queue.closed());
  EXPECT_TRUE(pusher.closed());
  EXPECT_FALSE(pusher.tryPush(2));

  // Pop remaining item then EOF
  auto item = queue.tryPop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 1);
  EXPECT_FALSE(queue.tryPop().has_value());
}

TEST_F(AsyncQueueTest, DirectResumptionSynchronousExecution) {
  AsyncQueue<int> queue(1);
  std::optional<int> popped_value;
  bool popper_resumed = false;

  auto popper_task = [&]() -> Task<absl::Status> {
    ASSIGN_OR_CO_RETURN(auto item, co_await queue.pop());
    popped_value = item;
    popper_resumed = true;
    co_return absl::OkStatus();
  };

  // Launch popper, which suspends waiting for item
  handles_.push_back(
      launch(popper_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  drain();
  EXPECT_FALSE(popper_resumed);
  EXPECT_FALSE(popped_value.has_value());

  // Push synchronously wakes up the popper inline during push!
  EXPECT_TRUE(queue.tryPush(42));
  EXPECT_TRUE(popper_resumed);
  EXPECT_EQ(popped_value, 42);
  EXPECT_TRUE(queue.empty());
}

TEST_F(AsyncQueueTest, PushPopRendezvousWhenBlockedOnCapacity) {
  // Shared capacity limit = 1 item
  auto shared_cap = std::make_shared<Capacity>(1);
  AsyncQueue<std::unique_ptr<int>> queue(shared_cap);

  // Fill capacity directly
  auto init_res = shared_cap->tryAcquire(1);
  ASSERT_TRUE(init_res.has_value());
  EXPECT_EQ(shared_cap->currentPermits(), 1);

  // Push into queue: capacity is full, so pusher suspends
  bool push_done = false;
  launchPush(queue, std::make_unique<int>(99), &push_done);
  drain();
  EXPECT_FALSE(push_done);
  EXPECT_EQ(queue.itemCount(), 1);
  EXPECT_EQ(queue.currentSize(), 1);
  EXPECT_FALSE(queue.empty());

  // Pop arrives while pusher is blocked on capacity:
  // pop() steals the item directly via rendezvous!
  std::optional<std::unique_ptr<int>> popped;
  launchPop(queue, popped);
  drain();

  ASSERT_TRUE(popped.has_value());
  ASSERT_NE(*popped, nullptr);
  EXPECT_EQ(**popped, 99);
  EXPECT_EQ(queue.itemCount(), 0);
  EXPECT_EQ(queue.currentSize(), 0);
  EXPECT_TRUE(queue.empty());

  // Releasing capacity unblocks the suspended pusher, which observes the item was
  // already delivered and completes cleanly with OkStatus without re-buffering.
  init_res->release();
  drain();

  EXPECT_TRUE(push_done);
  EXPECT_EQ(shared_cap->currentPermits(), 0);
  EXPECT_TRUE(queue.empty());
}

TEST_F(AsyncQueueTest, TryPushFailurePreservesCallerItem) {
  // Queue with capacity limit of 1, filled by holding capacity
  AsyncQueue<std::unique_ptr<int>> queue(1);
  auto hold = queue.capacity()->tryAcquire(1);
  ASSERT_TRUE(hold.has_value());

  auto item = std::make_unique<int>(123);
  EXPECT_FALSE(queue.tryPush(std::move(item)));
  // item must NOT be dropped on the floor
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(*item, 123);

  // Closed queue: tryPush will fail and preserve caller's item
  AsyncQueue<std::unique_ptr<int>> closed_queue(10);
  closed_queue.close();
  EXPECT_FALSE(closed_queue.tryPush(std::move(item)));
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(*item, 123);

  // When capacity is released, tryPush succeeds and moves item
  hold->release();
  EXPECT_TRUE(queue.tryPush(std::move(item)));
  EXPECT_EQ(item, nullptr);

  auto popped = queue.tryPop();
  ASSERT_TRUE(popped.has_value());
  ASSERT_NE(*popped, nullptr);
  EXPECT_EQ(**popped, 123);
}

TEST_F(AsyncQueueTest, QueueDestroyedWithStolenItemAndPendingPusher) {
  auto cap = std::make_shared<Capacity>(1);
  auto queue = std::make_unique<AsyncQueue<std::unique_ptr<int>>>(cap);

  auto hold = cap->tryAcquire(1);
  ASSERT_TRUE(hold.has_value());

  bool push_done = false;
  handles_.push_back(
      launch(pushTask(*queue, std::make_unique<int>(42), &push_done), executor_,
             [](absl::Status status) { EXPECT_TRUE(absl::IsFailedPrecondition(status)); }));
  drain();

  // Item is in queue_ pending capacity
  EXPECT_EQ(queue->itemCount(), 1);

  // Consumer steals item via rendezvous
  std::optional<std::unique_ptr<int>> popped;
  launchPop(*queue, popped);
  drain();

  ASSERT_TRUE(popped.has_value());
  ASSERT_NE(*popped, nullptr);
  EXPECT_EQ(**popped, 42);

  // Destroy queue while pusher is still suspended on capacity!
  queue.reset();

  // Now release capacity: pusher wakes up, sees !*alive, and completes cleanly
  hold->release();
  drain();

  EXPECT_FALSE(push_done); // Completed with FailedPreconditionError, push_done not set
}

} // namespace
} // namespace Coroutine
} // namespace Envoy
