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

} // namespace
} // namespace Coroutine
} // namespace Envoy
