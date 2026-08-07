#include <vector>

#include "source/common/protobuf/message_validator_impl.h"
#include "source/extensions/queue_policy/fifo/fifo_queue_policy.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

namespace {
class FifoQueueItemType : public ConnectionPool::Cancellable,
                          public LinkedObject<FifoQueueItemType> {
public:
  FifoQueueItemType(int value) : value_(value) {};
  ~FifoQueueItemType() override = default;
  int value() const { return value_; }
  // ConnectionPool::Cancellable
  void cancel(ConnectionPool::CancelPolicy) override {}

private:
  int value_;
};
} // namespace

using FifoQueuePolicyConfig = envoy::extensions::queue_policy::fifo::v3::FifoQueuePolicyConfig;

TEST(FifoQueueTest, TestQueueFunctions) {
  FifoQueue<FifoQueueItemType> queue;
  EXPECT_TRUE(queue.empty());
  queue.add(std::make_unique<FifoQueueItemType>(11), {MonotonicTime{}});
  queue.add(std::make_unique<FifoQueueItemType>(42), {MonotonicTime{}});
  EXPECT_FALSE(queue.isOverloaded());
  EXPECT_EQ(queue.size(), 2);
  EXPECT_EQ(queue.next().value(), 11);

  // forEach visits items in dequeue (FIFO) order.
  std::vector<int> visited;
  queue.forEach([&visited](FifoQueueItemType& item) -> bool {
    visited.push_back(item.value());
    return true;
  });
  EXPECT_EQ(visited, (std::vector<int>{11, 42}));

  // forEach stops early when the callback returns false.
  visited.clear();
  queue.forEach([&visited](FifoQueueItemType& item) -> bool {
    visited.push_back(item.value());
    return false;
  });
  EXPECT_EQ(visited, (std::vector<int>{11}));

  // The callback may safely remove the visited item.
  queue.forEach([&queue](FifoQueueItemType& item) -> bool {
    if (item.value() == 11) {
      queue.remove(item);
    }
    return true;
  });
  EXPECT_EQ(queue.size(), 1);
  EXPECT_EQ(queue.next().value(), 42);
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
