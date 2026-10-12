#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include "source/common/protobuf/message_validator_impl.h"
#include "source/extensions/queue_policy/adaptive_lifo/adaptive_lifo_queue_policy.h"

#include "absl/status/status.h"
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
  AdaptiveLifoQueue<TestQueueItem> queue(75, 25, []() -> uint64_t { return 4; });

  queue.add(first, {MonotonicTime{}});
  queue.add(second, {MonotonicTime{}});
  EXPECT_FALSE(queue.isOverloaded());
  EXPECT_EQ(queue.peek().value(), 1);

  queue.add(third, {MonotonicTime{}});
  EXPECT_TRUE(queue.isOverloaded());
  EXPECT_EQ(queue.peek().value(), 3);

  static_assert(std::is_same_v<decltype(queue.pop()), TestQueueItem&>);
  EXPECT_EQ(queue.pop().value(), 3);
  EXPECT_TRUE(queue.isOverloaded());
  EXPECT_EQ(queue.peek().value(), 2);

  EXPECT_EQ(queue.pop().value(), 2);
  EXPECT_FALSE(queue.isOverloaded());
  EXPECT_EQ(queue.peek().value(), 1);

  queue.add(fourth, {MonotonicTime{}});
  EXPECT_FALSE(queue.isOverloaded());
  const AdaptiveLifoQueue<TestQueueItem>& const_queue = queue;
  static_assert(std::is_same_v<decltype(const_queue.peek()), const TestQueueItem&>);
  EXPECT_EQ(const_queue.peek().value(), 1);

  queue.remove(first);
  EXPECT_FALSE(queue.isOverloaded());
  EXPECT_EQ(queue.pop().value(), 4);
  EXPECT_TRUE(queue.empty());
}

TEST(AdaptiveLifoQueueTest, ForEachUsesCurrentDequeueOrder) {
  TestQueueItem first(1);
  TestQueueItem second(2);
  TestQueueItem third(3);
  AdaptiveLifoQueue<TestQueueItem> queue(75, 25, []() -> uint64_t { return 4; });
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
  EXPECT_TRUE(queue.isOverloaded());
  EXPECT_EQ(queue.peek().value(), 3);
}

TEST(AdaptiveLifoQueueTest, ForEachStopsEarly) {
  TestQueueItem first(1);
  TestQueueItem second(2);
  AdaptiveLifoQueue<TestQueueItem> queue(100, 50, []() -> uint64_t { return 2; });
  queue.add(first, {MonotonicTime{}});
  queue.add(second, {MonotonicTime{}});

  std::vector<int> visited;
  queue.forEach([&visited](TestQueueItem& item) -> bool {
    visited.push_back(item.value());
    return false;
  });
  EXPECT_EQ(visited, (std::vector<int>{2}));
}

TEST(AdaptiveLifoQueueTest, ThresholdOneUsesLifoUntilEmpty) {
  TestQueueItem first(1);
  TestQueueItem second(2);
  AdaptiveLifoQueue<TestQueueItem> queue(50, 0, []() -> uint64_t { return 2; });

  queue.add(first, {MonotonicTime{}});
  EXPECT_TRUE(queue.isOverloaded());
  EXPECT_EQ(queue.peek().value(), 1);

  queue.add(second, {MonotonicTime{}});
  EXPECT_EQ(queue.pop().value(), 2);
  EXPECT_TRUE(queue.isOverloaded());
  EXPECT_EQ(queue.pop().value(), 1);
  EXPECT_FALSE(queue.isOverloaded());
  EXPECT_TRUE(queue.empty());
}

class AdaptiveLifoQueueFactoryTest : public testing::Test {
protected:
  absl::StatusOr<QueuePolicyUniquePtr<TestQueueItem>>
  createQueue(const AdaptiveLifoQueuePolicyConfig& config, uint64_t max_queue_size = 4) {
    return factory_.createQueuePolicy(config, "test_prefix",
                                      ProtobufMessage::getStrictValidationVisitor(),
                                      [max_queue_size]() { return max_queue_size; });
  }

  AdaptiveLifoQueueFactory<TestQueueItem> factory_;
};

TEST_F(AdaptiveLifoQueueFactoryTest, CreatesConfiguredQueue) {
  AdaptiveLifoQueuePolicyConfig config;
  config.mutable_high_watermark()->set_value(75);
  config.mutable_low_watermark()->set_value(25);
  auto result = createQueue(config);
  ASSERT_TRUE(result.ok());
  EXPECT_NE(result.value(), nullptr);
  EXPECT_EQ(factory_.name(), "envoy.queue_policy.adaptive_lifo");
}

TEST_F(AdaptiveLifoQueueFactoryTest, RejectsInvalidWatermarkRelationships) {
  const std::vector<std::pair<double, double>> invalid_watermarks = {{0, 0}, {50, 50}, {40, 50}};

  for (const auto& [high_watermark, low_watermark] : invalid_watermarks) {
    AdaptiveLifoQueuePolicyConfig config;
    config.mutable_high_watermark()->set_value(high_watermark);
    config.mutable_low_watermark()->set_value(low_watermark);
    auto result = createQueue(config);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(result.status().message(),
              "adaptive LIFO high_watermark must be greater than zero and low_watermark must be "
              "less than high_watermark");
  }
}

TEST_F(AdaptiveLifoQueueFactoryTest, RejectsMissingWatermarks) {
  AdaptiveLifoQueuePolicyConfig config;
  EXPECT_THROW(createQueue(config), ProtoValidationException);
}

TEST_F(AdaptiveLifoQueueFactoryTest, RejectsPercentageAboveOneHundred) {
  AdaptiveLifoQueuePolicyConfig config;
  config.mutable_high_watermark()->set_value(101);
  config.mutable_low_watermark()->set_value(50);
  EXPECT_THROW(createQueue(config), ProtoValidationException);
}

} // namespace
} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
