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
  queue.add(std::make_unique<FifoQueueItemType>(11));
  queue.add(std::make_unique<FifoQueueItemType>(42));
  EXPECT_FALSE(queue.isOverloaded());
  EXPECT_EQ(queue.next()->value(), 11);
  auto it = queue.begin();
  EXPECT_EQ((**it).value(), 11);
  ++it;
  EXPECT_NE(it, queue.end());
  EXPECT_EQ((**it).value(), 42);
  ++it;
  EXPECT_EQ(it, queue.end());
  queue.remove(**queue.begin());
  EXPECT_EQ(queue.next()->value(), 42);
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
