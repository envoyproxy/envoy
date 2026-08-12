#include <vector>

#include "source/common/protobuf/message_validator_impl.h"
#include "source/extensions/queue_policy/fifo/fifo_queue_policy.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

namespace {
class FifoQueueItemType {
public:
  FifoQueueItemType(int value) : value_(value) {}
  int value() const { return value_; }

private:
  int value_;
};
} // namespace

using FifoQueuePolicyConfig = envoy::extensions::queue_policy::fifo::v3::FifoQueuePolicyConfig;

TEST(FifoQueueTest, TestQueueFunctions) {
  FifoQueueItemType first(11);
  FifoQueueItemType second(42);
  FifoQueueItemType third(7);
  FifoQueue<FifoQueueItemType> queue;
  EXPECT_TRUE(queue.empty());
  queue.add(first, {MonotonicTime{}});
  queue.add(second, {MonotonicTime{}});
  queue.add(third, {MonotonicTime{}});
  EXPECT_FALSE(queue.isOverloaded());
  EXPECT_EQ(queue.size(), 3);
  EXPECT_EQ(queue.peek().value(), 11);

  // forEach visits items in dequeue (FIFO) order.
  std::vector<int> visited;
  queue.forEach([&visited](FifoQueueItemType& item) -> bool {
    visited.push_back(item.value());
    return true;
  });
  EXPECT_EQ(visited, (std::vector<int>{11, 42, 7}));

  // forEach stops early when the callback returns false.
  visited.clear();
  queue.forEach([&visited](FifoQueueItemType& item) -> bool {
    visited.push_back(item.value());
    return false;
  });
  EXPECT_EQ(visited, (std::vector<int>{11}));

  // The callback may safely remove the visited item.
  visited.clear();
  queue.forEach([&queue, &visited](FifoQueueItemType& item) -> bool {
    visited.push_back(item.value());
    if (item.value() == 42) {
      queue.remove(item);
    }
    return true;
  });
  EXPECT_EQ(visited, (std::vector<int>{11, 42, 7}));
  EXPECT_EQ(queue.size(), 2);
  EXPECT_EQ(queue.peek().value(), 11);
  EXPECT_EQ(second.value(), 42);

  queue.pop();
  EXPECT_EQ(queue.peek().value(), 7);
  queue.remove(third);
  EXPECT_TRUE(queue.empty());
}

class FifoQueueFactoryTest : public ::testing::Test {
protected:
  FifoQueueFactory<FifoQueueItemType> factory_;
};

TEST_F(FifoQueueFactoryTest, CanConstructFactory) {
  EXPECT_NO_THROW(FifoQueueFactory<FifoQueueItemType> f);
}

TEST_F(FifoQueueFactoryTest, CreateQueuePolicyReturnsValidPtr) {
  FifoQueuePolicyConfig config;
  auto result = factory_.createQueuePolicy(config, "test_prefix",
                                           ProtobufMessage::getStrictValidationVisitor());
  EXPECT_TRUE(result.ok());
  EXPECT_NE(result.value(), nullptr);
}

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
