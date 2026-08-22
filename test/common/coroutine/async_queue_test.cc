#include <memory>
#include <string>
#include <vector>

#include "source/common/coroutine/async_queue.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/task.h"

#include "test/common/coroutine/manual_executor.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

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

  std::shared_ptr<ManualExecutor> executor_;
  std::vector<DetachedHandle> handles_;
};

TEST_F(AsyncQueueTest, UnboundedQueuePushPopFIFO) {
  AsyncQueue<std::string> queue;

  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.currentSize(), 0);
  EXPECT_EQ(queue.itemCount(), 0);

  bool push_done = false;
  auto push_task = [&queue, &push_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await queue.push("item1"));
    CO_RETURN_IF_ERROR(co_await queue.push("item2"));
    CO_RETURN_IF_ERROR(co_await queue.push("item3"));
    push_done = true;
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();
  EXPECT_TRUE(push_done);
  EXPECT_EQ(queue.itemCount(), 3);
  EXPECT_EQ(queue.currentSize(), 3);

  std::vector<std::string> received;
  auto pop_task = [&queue, &received]() -> Task<absl::Status> {
    for (int i = 0; i < 3; ++i) {
      auto val_or = co_await queue.pop();
      if (!val_or.ok() || !val_or->has_value()) {
        break;
      }
      received.push_back(std::move(**val_or));
    }
    co_return absl::OkStatus();
  };

  handles_.push_back(launch(pop_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();
  ASSERT_EQ(received.size(), 3);
  EXPECT_EQ(received[0], "item1");
  EXPECT_EQ(received[1], "item2");
  EXPECT_EQ(received[2], "item3");
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

  handles_.push_back(
      launch(push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  // Pushes 1 and 2 completed; 3 is waiting in push_waiters_
  EXPECT_EQ(queue.itemCount(), 2);
  EXPECT_EQ(pushed.size(), 3);

  // Pop one item, which frees space and unblocks push of 3
  std::optional<int> pop1;
  auto pop_task1 = [&queue, &pop1]() -> Task<absl::Status> {
    auto val_or = co_await queue.pop();
    if (val_or.ok() && val_or->has_value()) {
      pop1 = std::move(**val_or);
    }
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(pop_task1(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();
  EXPECT_EQ(pop1, 1);

  // Now push of 3 has completed, and push of 4 suspended because queue is back to capacity (2,3)
  EXPECT_EQ(queue.itemCount(), 2);
  EXPECT_EQ(pushed.size(), 4);

  // Pop remaining items
  std::vector<int> remaining_popped;
  auto drain_task = [&queue, &remaining_popped]() -> Task<absl::Status> {
    for (int i = 0; i < 3; ++i) {
      auto val_or = co_await queue.pop();
      if (val_or.ok() && val_or->has_value()) {
        remaining_popped.push_back(**val_or);
      }
    }
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(drain_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();
  ASSERT_EQ(remaining_popped.size(), 3);
  EXPECT_EQ(remaining_popped[0], 2);
  EXPECT_EQ(remaining_popped[1], 3);
  EXPECT_EQ(remaining_popped[2], 4);
  EXPECT_TRUE(queue.empty());
}

TEST_F(AsyncQueueTest, AbstractCapacityUnitBytes) {
  // Capacity = 100 bytes
  AsyncQueue<TestByteItem, TestByteSizeFunc> queue(100);

  EXPECT_EQ(queue.currentSize(), 0);

  bool push1_done = false;
  bool push2_done = false;

  auto push_task = [&queue, &push1_done, &push2_done]() -> Task<absl::Status> {
    // 60 bytes
    CO_RETURN_IF_ERROR(co_await queue.push(TestByteItem{std::string(60, 'a')}));
    push1_done = true;

    // 50 bytes -> 60 + 50 = 110 > 100 bytes, suspends!
    CO_RETURN_IF_ERROR(co_await queue.push(TestByteItem{std::string(50, 'b')}));
    push2_done = true;
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  EXPECT_TRUE(push1_done);
  EXPECT_FALSE(push2_done);
  EXPECT_EQ(queue.currentSize(), 60);

  // Pop first item (60 bytes)
  auto pop_task = [&queue]() -> Task<absl::Status> {
    auto val_or = co_await queue.pop();
    EXPECT_TRUE(val_or.ok());
    EXPECT_EQ((*val_or)->data.size(), 60);
    co_return absl::OkStatus();
  };

  handles_.push_back(launch(pop_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  // Now push2 should have unblocked
  EXPECT_TRUE(push2_done);
  EXPECT_EQ(queue.currentSize(), 50);
  EXPECT_EQ(queue.itemCount(), 1);
}

TEST_F(AsyncQueueTest, DirectHandoffToWaitingPopper) {
  AsyncQueue<std::string> queue(1);

  std::optional<std::string> popped;
  auto pop_task = [&queue, &popped]() -> Task<absl::Status> {
    auto val_or = co_await queue.pop();
    if (val_or.ok() && val_or->has_value()) {
      popped = std::move(**val_or);
    }
    co_return absl::OkStatus();
  };

  handles_.push_back(launch(pop_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();
  EXPECT_FALSE(popped.has_value());

  // Push directly hands off to the waiting popper without queueing
  auto push_task = [&queue]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await queue.push("direct_message"));
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();
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

TEST_F(AsyncQueueTest, CloseSignalsEOF) {
  AsyncQueue<std::string> queue;

  queue.tryPush("msg");
  queue.close();

  // Subsequent push fails
  EXPECT_FALSE(queue.tryPush("msg2"));

  // First pop gets queued item
  std::optional<std::string> val1;
  auto pop_task1 = [&queue, &val1]() -> Task<absl::Status> {
    auto res = co_await queue.pop();
    if (res.ok() && res->has_value()) {
      val1 = std::move(**res);
    }
    co_return absl::OkStatus();
  };
  handles_.push_back(
      launch(pop_task1(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();
  EXPECT_EQ(val1, "msg");

  // Second pop gets EOF (nullopt)
  bool eof_seen = false;
  auto pop_task2 = [&queue, &eof_seen]() -> Task<absl::Status> {
    auto res = co_await queue.pop();
    if (res.ok() && !res->has_value()) {
      eof_seen = true;
    }
    co_return absl::OkStatus();
  };
  handles_.push_back(
      launch(pop_task2(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();
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

  handles_.push_back(
      launch(push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  queue.close();
  executor_->drain();

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
  executor_->drain();
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
  executor_->drain();
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
  auto push_task = [&q2, &q2_push_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q2.push(40));
    q2_push_done = true;
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  EXPECT_FALSE(q2_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 3);

  // Pop from q1, which frees 1 slot in shared capacity and unblocks q2's push
  auto pop1 = q1.tryPop();
  ASSERT_TRUE(pop1.has_value());
  EXPECT_EQ(*pop1, 10);

  executor_->drain();
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
  auto push_task = [&q1, &q1_push_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q1.push(TestByteItem{std::string(30, 'c')}));
    q1_push_done = true;
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  EXPECT_FALSE(q1_push_done);

  // Pop 40 bytes from q2
  auto pop_item = q2.tryPop();
  ASSERT_TRUE(pop_item.has_value());
  EXPECT_EQ(pop_item->data.size(), 40);

  executor_->drain();
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
  auto push_task = [&q2, &q2_push_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q2.push(3));
    q2_push_done = true;
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();
  EXPECT_FALSE(q2_push_done);

  // Destroying q1 releases its 2 units from shared capacity
  q1.reset();

  executor_->drain();
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

  handles_.push_back(
      launch(push_and_pop(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();
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
  auto pop_task = [&q2, &q2_popped]() -> Task<absl::Status> {
    auto res = co_await q2.pop();
    if (res.ok() && res->has_value()) {
      q2_popped = **res;
    }
    co_return absl::OkStatus();
  };

  handles_.push_back(launch(pop_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();
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
  auto push_task = [&q2, &q2_push_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q2.push(200));
    q2_push_done = true;
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();
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

  handles_.push_back(
      launch(filter1_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(
      launch(filter2_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(
      launch(sink_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));

  // Push 3 items into Q1
  auto make_push_task = [&q1](int val) -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q1.push(val));
    co_return absl::OkStatus();
  };

  for (int i = 1; i <= 3; ++i) {
    handles_.push_back(
        launch(make_push_task(i), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  }

  executor_->drain();
  q1.close();
  executor_->drain();

  // (1*10+1=11, 2*10+1=21, 3*10+1=31)
  ASSERT_EQ(sink_received.size(), 3);
  EXPECT_EQ(sink_received[0], 11);
  EXPECT_EQ(sink_received[1], 21);
  EXPECT_EQ(sink_received[2], 31);
}

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
  auto q2_push_task = [&q2, &q2_push_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q2.push(100));
    q2_push_done = true;
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(q1_push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(
      launch(q2_push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  EXPECT_FALSE(q1_push3_done);
  EXPECT_FALSE(q2_push_done);

  // Pop from q1: frees 1 slot.
  // This triggers notifySpaceAvailable() -> unblocks q1_push_task.
  // q1_push_task runs and performs a re-entrant pop from q1 -> frees another slot while notifying!
  // q2_push_task must also be unblocked without lost wakeup.
  auto item = q1.tryPop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 1);

  executor_->drain();

  EXPECT_TRUE(q1_push3_done);
  EXPECT_TRUE(q2_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 2);
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
  auto make_push_task = [&](AsyncQueue<std::string>& q, std::string tag) -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q.push(tag));
    order_granted.push_back(tag);
    co_return absl::OkStatus();
  };

  handles_.push_back(launch(make_push_task(q1, "q1_1"), executor_,
                            [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(launch(make_push_task(q2, "q2_1"), executor_,
                            [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(launch(make_push_task(q3, "q3_1"), executor_,
                            [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(launch(make_push_task(q1, "q1_2"), executor_,
                            [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(launch(make_push_task(q3, "q3_2"), executor_,
                            [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(launch(make_push_task(q2, "q2_2"), executor_,
                            [](absl::Status status) { EXPECT_OK(status); }));

  executor_->drain();
  EXPECT_TRUE(order_granted.empty());

  // Pop initial item to start granting capacity
  auto pop1 = q1.tryPop();
  ASSERT_TRUE(pop1.has_value());
  EXPECT_EQ(*pop1, "init");
  executor_->drain();

  // Pop whichever queue currently holds the item to free capacity for the next in global FIFO order
  for (int i = 0; i < 6; ++i) {
    if (!q1.empty()) {
      q1.tryPop();
    } else if (!q2.empty()) {
      q2.tryPop();
    } else if (!q3.empty()) {
      q3.tryPop();
    }
    executor_->drain();
  }

  // All 6 pushes should have been granted in strict temporal global FIFO order across queues:
  // q1_1 -> q2_1 -> q3_1 -> q1_2 -> q3_2 -> q2_2
  ASSERT_EQ(order_granted.size(), 6);
  EXPECT_EQ(order_granted[0], "q1_1");
  EXPECT_EQ(order_granted[1], "q2_1");
  EXPECT_EQ(order_granted[2], "q3_1");
  EXPECT_EQ(order_granted[3], "q1_2");
  EXPECT_EQ(order_granted[4], "q3_2");
  EXPECT_EQ(order_granted[5], "q2_2");
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
  auto q2_push_task = [&q2, &q2_push_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q2.push(20));
    q2_push_done = true;
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(q2_push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();
  EXPECT_FALSE(q2_push_done);

  // Pop remaining item from closed q1 -> frees capacity
  auto val = q1->tryPop();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 10);

  executor_->drain();
  // q2 should have received capacity notification and unblocked
  EXPECT_TRUE(q2_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 1);
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
  auto q2_push_task = [&q2, &q2_push_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q2.push(10));
    q2_push_done = true;
    co_return absl::OkStatus();
  };

  handles_.push_back(launch(q1_push_task(), executor_, [](absl::Status status) {
    EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
  }));
  handles_.push_back(
      launch(q2_push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  // Reset q1, destroying it
  q1.reset();
  executor_->drain();

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

  handles_.push_back(
      launch(push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();
  EXPECT_FALSE(push_completed);

  // Pop item 1: this unblocks pusher for item 2, which then immediately calls q.reset()!
  // This must execute safely without Use-After-Free.
  auto val = q->tryPop();
  ASSERT_TRUE(val.has_value());
  EXPECT_EQ(*val, 1);

  executor_->drain();
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
  executor_->drain();
  EXPECT_FALSE(handler_ran);

  // Close queue, which aborts push waiter, which then deletes q
  q->close();
  executor_->drain();

  EXPECT_TRUE(handler_ran);
  EXPECT_EQ(q, nullptr);
}

TEST_F(AsyncQueueTest, DynamicQueueCreationDuringNotification) {
  auto shared_cap = std::make_shared<SharedCapacity>(2);
  auto q1 = std::make_unique<AsyncQueue<int>>(shared_cap);
  EXPECT_TRUE(q1->tryPush(1));
  EXPECT_TRUE(q1->tryPush(2));

  std::unique_ptr<AsyncQueue<int>> dynamic_q;
  bool dynamic_push_done = false;

  auto push_task = [&q1, &shared_cap, &dynamic_q, &dynamic_push_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q1->push(3));
    // Create a new AsyncQueue dynamically while notification was running
    dynamic_q = std::make_unique<AsyncQueue<int>>(shared_cap);
    // Queue is full (items 2 and 3 in q1), so pushing into dynamic_q suspends
    CO_RETURN_IF_ERROR(co_await dynamic_q->push(100));
    dynamic_push_done = true;
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();
  EXPECT_FALSE(dynamic_push_done);

  // Pop item 1 from q1 -> unblocks push(3) to q1, dynamic_q created and waits on push(100)
  EXPECT_TRUE(q1->tryPop().has_value());
  executor_->drain();
  EXPECT_FALSE(dynamic_push_done);
  ASSERT_NE(dynamic_q, nullptr);

  // Pop item 2 from q1 -> frees capacity -> dynamic_q receives space notification and unblocks
  EXPECT_TRUE(q1->tryPop().has_value());
  executor_->drain();

  EXPECT_TRUE(dynamic_push_done);
  EXPECT_EQ(dynamic_q->itemCount(), 1);
  auto item = dynamic_q->tryPop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 100);
}

TEST_F(AsyncQueueTest, FairnessPreservedAcrossDynamicQueueDestruction) {
  // Shared capacity limit = 1 unit
  auto shared_cap = std::make_shared<SharedCapacity>(1);
  auto q0 = std::make_unique<AsyncQueue<std::string>>(shared_cap);
  auto q1 = std::make_unique<AsyncQueue<std::string>>(shared_cap);
  auto q2 = std::make_unique<AsyncQueue<std::string>>(shared_cap);

  // Fill capacity with q0
  EXPECT_TRUE(q0->tryPush("q0_init"));
  EXPECT_EQ(shared_cap->currentSize(), 1);

  std::vector<std::string> order_granted;

  auto make_push_task = [&](AsyncQueue<std::string>& q, std::string tag) -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q.push(tag));
    order_granted.push_back(tag);
    co_return absl::OkStatus();
  };

  // Launch pushes in order: q0, q1, q2
  handles_.push_back(launch(make_push_task(*q0, "q0_push"), executor_,
                            [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(launch(make_push_task(*q1, "q1_push"), executor_,
                            [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(launch(make_push_task(*q2, "q2_push"), executor_,
                            [](absl::Status status) { EXPECT_OK(status); }));

  executor_->drain();
  EXPECT_TRUE(order_granted.empty());

  // Pop q0_init. This starts notification round: q0 gets space and unblocks q0_push.
  auto pop0 = q0->tryPop();
  ASSERT_TRUE(pop0.has_value());
  EXPECT_EQ(*pop0, "q0_init");
  executor_->drain();

  ASSERT_EQ(order_granted.size(), 1);
  EXPECT_EQ(order_granted[0], "q0_push");

  // Now destroy q0. In doing so, q0's callback is removed, shifting q1 and q2 in the array.
  // Next in round-robin order MUST be q1, not q2.
  q0.reset();
  executor_->drain();

  // Add another push to q1 to test ordering
  handles_.push_back(launch(make_push_task(*q1, "q1_push2"), executor_,
                            [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  // q1 should get the space next
  ASSERT_EQ(order_granted.size(), 2);
  EXPECT_EQ(order_granted[1], "q1_push");

  // Now pop q1's item -> q2 MUST get space next, NOT q1_push2.
  auto pop1 = q1->tryPop();
  ASSERT_TRUE(pop1.has_value());
  EXPECT_EQ(*pop1, "q1_push");
  executor_->drain();

  ASSERT_EQ(order_granted.size(), 3);
  EXPECT_EQ(order_granted[2], "q2_push");

  // Now pop q2's item -> q1_push2 gets space next
  auto pop2 = q2->tryPop();
  ASSERT_TRUE(pop2.has_value());
  EXPECT_EQ(*pop2, "q2_push");
  executor_->drain();

  ASSERT_EQ(order_granted.size(), 4);
  EXPECT_EQ(order_granted[3], "q1_push2");
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
  auto push1_task = [&q2, &push1_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q2.push(TestByteItem{std::string(20, 'b')}));
    push1_done = true;
    co_return absl::OkStatus();
  };

  // Push 2 on q2: 2 bytes -> would fit in 5 free units, but suspended behind push 1
  auto push2_task = [&q2, &push2_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q2.push(TestByteItem{std::string(2, 'c')}));
    push2_done = true;
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(push1_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(
      launch(push2_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  EXPECT_FALSE(push1_done);
  EXPECT_FALSE(push2_done);

  // Direct handoff pop from q2: pops push 1 directly without using capacity
  auto popped = q2.tryPop();
  ASSERT_TRUE(popped.has_value());
  EXPECT_EQ(popped->data.size(), 20);

  executor_->drain();

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

  // Push 2 on q2: 3 bytes -> would fit in 5 free units, but suspended behind push 1
  auto push2_task = [&q2, &push2_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q2.push(TestByteItem{std::string(3, 'c')}));
    push2_done = true;
    co_return absl::OkStatus();
  };

  DetachedHandle h1 = launch(push1_task(), executor_,
                             [](absl::Status status) { EXPECT_TRUE(absl::IsCancelled(status)); });
  handles_.push_back(
      launch(push2_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  EXPECT_FALSE(push1_status.has_value());
  EXPECT_FALSE(push2_done);

  // Cancel push 1
  h1.cancel();
  ASSERT_TRUE(push1_status.has_value());
  EXPECT_TRUE(absl::IsCancelled(*push1_status));

  executor_->drain();

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
  auto large_push_task = [&q1, &large_push_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q1.push(TestByteItem{std::string(80, 'b')}));
    large_push_done = true;
    co_return absl::OkStatus();
  };

  // Waiter 2 (q2): small chunk of 20 bytes (would fit into 40 free bytes, but behind Waiter 1 in
  // FIFO)
  auto small_push_task = [&q2, &small_push_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q2.push(TestByteItem{std::string(20, 'c')}));
    small_push_done = true;
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(large_push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(
      launch(small_push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  // Strict Head-of-Line FIFO: small push MUST NOT bypass the large push ahead of it
  EXPECT_FALSE(large_push_done);
  EXPECT_FALSE(small_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 60);

  // Pop 60 bytes from q1 -> current_size becomes 0 -> 100 bytes free
  auto item = q1.tryPop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(item->data.size(), 60);

  executor_->drain();

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
  auto oversized_push_task = [&q2, &oversized_push_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q2.push(TestByteItem{std::string(100, 'b')}));
    oversized_push_done = true;
    co_return absl::OkStatus();
  };

  // Waiter 2 (q1): normal chunk of 10 bytes (behind oversized chunk in wait queue)
  auto normal_push_task = [&q1, &normal_push_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q1.push(TestByteItem{std::string(10, 'c')}));
    normal_push_done = true;
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(oversized_push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(
      launch(normal_push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  EXPECT_FALSE(oversized_push_done);
  EXPECT_FALSE(normal_push_done);

  // Pop 30 bytes from q1 -> current_size becomes 0
  auto item1 = q1.tryPop();
  ASSERT_TRUE(item1.has_value());
  EXPECT_EQ(item1->data.size(), 30);

  executor_->drain();

  // Oversized chunk is granted when current_size == 0
  EXPECT_TRUE(oversized_push_done);
  // Normal chunk is still waiting because current_size is 100 >= 50
  EXPECT_FALSE(normal_push_done);
  EXPECT_EQ(shared_cap->currentSize(), 100);

  // Pop the oversized chunk from q2 -> current_size becomes 0
  auto item2 = q2.tryPop();
  ASSERT_TRUE(item2.has_value());
  EXPECT_EQ(item2->data.size(), 100);

  executor_->drain();

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

  auto w1_task = [&q2, &w1_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q2.push(101));
    w1_done = true;
    co_return absl::OkStatus();
  };

  auto w2_task = [&q3, &w2_status]() -> Task<absl::Status> {
    w2_status = co_await q3.push(102);
    co_return *w2_status;
  };

  auto w3_task = [&q2, &w3_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q2.push(103));
    w3_done = true;
    co_return absl::OkStatus();
  };

  handles_.push_back(launch(w1_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  DetachedHandle h2 = launch(w2_task(), executor_,
                             [](absl::Status status) { EXPECT_TRUE(absl::IsCancelled(status)); });
  handles_.push_back(launch(w3_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  EXPECT_FALSE(w1_done);
  EXPECT_FALSE(w2_status.has_value());
  EXPECT_FALSE(w3_done);

  // Cancel middle waiter (w2)
  h2.cancel();
  ASSERT_TRUE(w2_status.has_value());
  EXPECT_TRUE(absl::IsCancelled(*w2_status));

  // Pop 1 item from q1 -> frees 1 unit -> w1 (head) should be granted
  EXPECT_TRUE(q1.tryPop().has_value());
  executor_->drain();

  EXPECT_TRUE(w1_done);
  EXPECT_FALSE(w3_done);

  // Pop 1 item from q1 -> frees 1 unit -> w3 (tail) should be granted (since w2 was cancelled)
  EXPECT_TRUE(q1.tryPop().has_value());
  executor_->drain();

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

  // Next waiter needs 2 units (fits within 3 free units, but blocked by head)
  bool next_done = false;
  auto next_task = [&q_bytes, &next_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q_bytes.push(TestByteItem{std::string(2, 'y')}));
    next_done = true;
    co_return absl::OkStatus();
  };

  DetachedHandle h_head = launch(
      head_task(), executor_, [](absl::Status status) { EXPECT_TRUE(absl::IsCancelled(status)); });
  handles_.push_back(
      launch(next_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  EXPECT_FALSE(head_status.has_value());
  EXPECT_FALSE(next_done);

  // Cancel head waiter: next waiter should be granted immediately without popping!
  h_head.cancel();
  ASSERT_TRUE(head_status.has_value());
  EXPECT_TRUE(absl::IsCancelled(*head_status));

  executor_->drain();
  EXPECT_TRUE(next_done);
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
  auto q2_w1_task = [&q2, &q2_w1_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q2->push(10));
    q2_w1_done = true;
    co_return absl::OkStatus();
  };

  // q2 pushes w2
  auto q2_w2_task = [&q2, &q2_w2_status]() -> Task<absl::Status> {
    q2_w2_status = co_await q2->push(20);
    co_return q2_w2_status;
  };

  // q3 pushes w3
  auto q3_w_task = [&q3, &q3_w_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q3.push(30));
    q3_w_done = true;
    co_return absl::OkStatus();
  };

  handles_.push_back(
      launch(q2_w1_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(launch(q2_w2_task(), executor_, [](absl::Status status) {
    EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
  }));
  handles_.push_back(
      launch(q3_w_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

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
  executor_->drain();
  EXPECT_THAT(q2_w2_status, HasStatusCode(absl::StatusCode::kFailedPrecondition));

  // Now pop 1 item from q1 -> frees 1 unit -> q3_w should be granted
  EXPECT_TRUE(q1.tryPop().has_value());
  executor_->drain();

  EXPECT_TRUE(q3_w_done);
  EXPECT_EQ(q3.itemCount(), 1);
  EXPECT_EQ(shared_cap->currentSize(), 5); // 4 in q1 + 1 in q3
}

TEST_F(AsyncQueueTest, HasCapacityOverflowProtection) {
  SharedCapacity cap(100);
  EXPECT_TRUE(cap.tryAcquire(50));
  EXPECT_TRUE(cap.hasCapacity(50));
  EXPECT_FALSE(cap.hasCapacity(51));
  EXPECT_FALSE(cap.hasCapacity(std::numeric_limits<uint64_t>::max()));
  EXPECT_FALSE(cap.hasCapacity(std::numeric_limits<uint64_t>::max() - 10));
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

  // q3 pushes w3: 2 units (would fit in 9 free units, but suspended behind q2's waiters)
  auto q3_w_task = [&q3, &q3_w_done]() -> Task<absl::Status> {
    CO_RETURN_IF_ERROR(co_await q3.push(TestByteItem{std::string(2, 'd')}));
    q3_w_done = true;
    co_return absl::OkStatus();
  };

  handles_.push_back(launch(q2_w1_task(), executor_, [](absl::Status status) {
    EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
  }));
  handles_.push_back(launch(q2_w2_task(), executor_, [](absl::Status status) {
    EXPECT_THAT(status, HasStatusCode(absl::StatusCode::kFailedPrecondition));
  }));
  handles_.push_back(
      launch(q3_w_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  EXPECT_FALSE(q3_w_done);

  // Destroy q2.
  // Both q2_w1 and q2_w2 must be cancelled from SharedCapacity without w2 being granted to q2,
  // and q3_w must be granted the capacity immediately upon unblocking the head!
  q2.reset();
  executor_->drain();

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

  handles_.push_back(launch(task1(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(launch(task2(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  handles_.push_back(launch(task3(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  EXPECT_TRUE(execution_order.empty());

  // Pop from q1: frees 1 slot -> triggers cascade of deep re-entrant grants across q1, q2, q3
  auto item = q1.tryPop();
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(*item, 1);

  executor_->drain();

  // Tasks 1, 2, 3 have their initial pushes granted in order (1, 3, 5).
  // Then Task 1's re-entrant push to q2 is granted (2).
  // Task 2's re-entrant push to q3 is now waiting at the head of the wait queue.
  ASSERT_EQ(execution_order.size(), 4);
  EXPECT_EQ(execution_order[0], 1);
  EXPECT_EQ(execution_order[1], 3);
  EXPECT_EQ(execution_order[2], 5);
  EXPECT_EQ(execution_order[3], 2);

  // Pop from q2: frees 1 slot -> Task 2's re-entrant push to q3 is granted (4)
  auto item2 = q2.tryPop();
  ASSERT_TRUE(item2.has_value());

  executor_->drain();

  ASSERT_EQ(execution_order.size(), 5);
  EXPECT_EQ(execution_order[4], 4);
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
  handles_.push_back(
      launch(q2_push_task(), executor_, [](absl::Status status) { EXPECT_OK(status); }));
  executor_->drain();

  EXPECT_FALSE(q1_push_status.has_value());
  EXPECT_FALSE(q2_push_done);

  // Cancel q1's push: this cancels q1 from head of SharedCapacity, which unblocks q2's push.
  // q2's push immediately destroys q1.
  // removePushWaiter must safely handle q1 having been destroyed during cancellation!
  h1.cancel();
  ASSERT_TRUE(q1_push_status.has_value());
  EXPECT_TRUE(absl::IsCancelled(*q1_push_status));

  executor_->drain();
  EXPECT_TRUE(q2_push_done);
  EXPECT_EQ(q1, nullptr);
  EXPECT_EQ(shared_cap->currentSize(), 1);
}

TEST_F(AsyncQueueTest, NullCallbackInRequestCapacityDefensiveHandling) {
  SharedCapacity cap(10);
  EXPECT_EQ(cap.currentSize(), 0);

  // In debug builds, passing a null callback triggers ASSERT.
  EXPECT_DEBUG_DEATH({ cap.requestCapacity(5, nullptr); }, "assert failure: cb != nullptr");

  // Subsequent valid requests should operate normally and acquire capacity.
  bool valid_called = false;
  cap.requestCapacity(5, [&valid_called]() { valid_called = true; });
  cap.release(0); // Trigger processWaiters()
  EXPECT_TRUE(valid_called);
  EXPECT_EQ(cap.currentSize(), 5);

  cap.release(5);
  EXPECT_EQ(cap.currentSize(), 0);
}

} // namespace
} // namespace Coroutine
} // namespace Envoy
