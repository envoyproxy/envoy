#include <type_traits>
#include <vector>

#include "source/common/protobuf/message_validator_impl.h"
#include "source/extensions/queue_policy/adaptive_lifo/adaptive_lifo_queue_policy.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {
namespace {

class TestQueueItem {
public:
  explicit TestQueueItem(int value) : value_(value) {}
  int value() const { return value_; }

private:
  const int value_;
};

using AdaptiveLifoQueuePolicyConfig =
    envoy::extensions::queue_policy::adaptive_lifo::v3::AdaptiveLifoQueuePolicyConfig;

TEST(AdaptiveLifoQueueTest, SwitchesBetweenFifoAndLifo) {
  TestQueueItem first(1);
  TestQueueItem second(2);
  TestQueueItem third(3);
  TestQueueItem fourth(4);
  AdaptiveLifoQueue<TestQueueItem> queue(3);

  queue.add(first, {MonotonicTime{}});
  queue.add(second, {MonotonicTime{}});
  EXPECT_FALSE(queue.isOverloaded());
  EXPECT_EQ(queue.peek().value(), 1);

  queue.add(third, {MonotonicTime{}});
  EXPECT_TRUE(queue.isOverloaded());
  EXPECT_EQ(queue.peek().value(), 3);

  static_assert(std::is_same_v<decltype(queue.pop()), TestQueueItem&>);
  EXPECT_EQ(queue.pop().value(), 3);
  EXPECT_FALSE(queue.isOverloaded());
  EXPECT_EQ(queue.peek().value(), 1);

  queue.add(fourth, {MonotonicTime{}});
  EXPECT_TRUE(queue.isOverloaded());
  const AdaptiveLifoQueue<TestQueueItem>& const_queue = queue;
  static_assert(std::is_same_v<decltype(const_queue.peek()), const TestQueueItem&>);
  EXPECT_EQ(const_queue.peek().value(), 4);

  queue.remove(second);
  EXPECT_FALSE(queue.isOverloaded());
  EXPECT_EQ(queue.pop().value(), 1);
  EXPECT_EQ(queue.pop().value(), 4);
  EXPECT_TRUE(queue.empty());
}

TEST(AdaptiveLifoQueueTest, ForEachUsesCurrentDequeueOrder) {
  TestQueueItem first(1);
  TestQueueItem second(2);
  TestQueueItem third(3);
  AdaptiveLifoQueue<TestQueueItem> queue(3);
  queue.add(first, {MonotonicTime{}});
  queue.add(second, {MonotonicTime{}});

  std::vector<int> visited;
  queue.forEach([&visited](TestQueueItem& item) -> bool {
    visited.push_back(item.value());
    return true;
  });
  EXPECT_EQ(visited, (std::vector<int>{1, 2}));

  queue.add(third, {MonotonicTime{}});
  visited.clear();
  queue.forEach([&queue, &visited](TestQueueItem& item) -> bool {
    visited.push_back(item.value());
    if (item.value() == 2) {
      queue.remove(item);
    }
    return true;
  });
  EXPECT_EQ(visited, (std::vector<int>{3, 2, 1}));
  EXPECT_EQ(queue.size(), 2);
  EXPECT_EQ(queue.peek().value(), 1);
}

TEST(AdaptiveLifoQueueTest, ForEachStopsEarly) {
  TestQueueItem first(1);
  TestQueueItem second(2);
  AdaptiveLifoQueue<TestQueueItem> queue(2);
  queue.add(first, {MonotonicTime{}});
  queue.add(second, {MonotonicTime{}});

  std::vector<int> visited;
  queue.forEach([&visited](TestQueueItem& item) -> bool {
    visited.push_back(item.value());
    return false;
  });
  EXPECT_EQ(visited, (std::vector<int>{2}));
}

class AdaptiveLifoQueueFactoryTest : public testing::Test {
protected:
  AdaptiveLifoQueueFactory<TestQueueItem> factory_;
};

TEST_F(AdaptiveLifoQueueFactoryTest, CreatesConfiguredQueue) {
  AdaptiveLifoQueuePolicyConfig config;
  config.set_lifo_switch_threshold(2);
  auto result = factory_.createQueuePolicy(config, "test_prefix",
                                           ProtobufMessage::getStrictValidationVisitor());
  ASSERT_TRUE(result.ok());
  EXPECT_NE(result.value(), nullptr);
  EXPECT_EQ(factory_.name(), "envoy.queue_policy.adaptive_lifo");
}

} // namespace
} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
