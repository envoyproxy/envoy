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
  EXPECT_TRUE(cap->tryAcquire(2));
  EXPECT_FALSE(cap->tryAcquire(1));
  EXPECT_EQ(cap->currentSize(), 2);

  cap->release(1);
  EXPECT_EQ(cap->currentSize(), 1);
  EXPECT_TRUE(cap->tryAcquire(1));

  cap->release(2);
  EXPECT_EQ(cap->currentSize(), 0);
}

TEST_F(AsyncQueueTest, SharedCapacityRequestAndCancel) {
  auto cap = std::make_shared<SharedCapacity>(1);
  EXPECT_TRUE(cap->tryAcquire(1));

  bool task1_done = false;
  bool task2_done = false;
  std::optional<absl::Status> task1_status;

  auto t1 = [&]() -> Task<absl::Status> {
    task1_status = co_await cap->acquire(1);
    task1_done = true;
    co_return *task1_status;
  };
  auto t2 = [&]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await cap->acquire(1));
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
  cap->release(1);
  drain();

  EXPECT_TRUE(task2_done);
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

TEST_F(AsyncQueueTest, SharedCapacityDestructionAbortsAcquireWaiters) {
  auto cap = std::make_shared<SharedCapacity>(1);
  EXPECT_TRUE(cap->tryAcquire(1));

  absl::Status waiter_status;
  auto acquire_task = [&cap, &waiter_status]() -> Task<absl::Status> {
    waiter_status = co_await cap->acquire(1);
    co_return waiter_status;
  };

  handles_.push_back(launch(acquire_task(), executor_, [](absl::Status) {}));
  drain();
  EXPECT_TRUE(waiter_status.ok());

  // Destroy cap
  cap.reset();
  drain();
  EXPECT_THAT(waiter_status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
}

TEST_F(AsyncQueueTest, SharedCapacityReleaseZero) {
  auto cap = std::make_shared<SharedCapacity>(10);
  EXPECT_TRUE(cap->tryAcquire(5));
  EXPECT_EQ(cap->currentSize(), 5);

  // Release 0 is a no-op
  cap->release(0);
  EXPECT_EQ(cap->currentSize(), 5);

  cap->release(5);
  EXPECT_EQ(cap->currentSize(), 0);
}

} // namespace
} // namespace Coroutine
} // namespace Envoy
