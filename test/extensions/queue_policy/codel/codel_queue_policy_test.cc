#include <chrono>
#include <memory>

#include "envoy/extensions/queue_policy/fifo/v3/fifo.pb.h"

#include "source/common/conn_pool/pending_stream.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/queue_policy/adaptive_lifo_queue.h"
#include "source/common/queue_policy/codel_queue.h"
#include "source/common/queue_policy/fifo_queue_policy.h"
#include "source/extensions/queue_policy/codel/codel_queue_policy.h"

#include "test/test_common/simulated_time_system.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {
namespace {

using namespace std::chrono_literals;
using CodelQueuePolicyConfig = envoy::extensions::queue_policy::codel::v3::CodelQueuePolicyConfig;

class TestItem {
public:
  explicit TestItem(int value) : value_(value) {}
  int value() const { return value_; }

private:
  const int value_;
};

class AlwaysDropFifoQueue : public FifoQueue<TestItem> {
public:
  DequeueAction onDequeue(TestItem&, MonotonicTime) override { return DequeueAction::Drop; }
};

class CodelQueueTest : public testing::Test {
protected:
  MonotonicTime now() { return time_system_.monotonicTime(); }
  void advance(std::chrono::milliseconds duration) { time_system_.advanceTimeWait(duration); }

  Event::SimulatedTimeSystemHelper time_system_;
};

TEST_F(CodelQueueTest, DropsOnlyAfterSustainedDelayAndResetsWhenDrained) {
  CodelQueue<TestItem> queue(std::make_unique<FifoQueue<TestItem>>(), 5ms, 100ms);
  TestItem first(1);
  TestItem second(2);
  TestItem third(3);
  TestItem fourth(4);

  const MonotonicTime start = now();
  queue.add(first, {start});
  queue.add(second, {start});
  queue.add(third, {start});
  advance(10ms);
  EXPECT_EQ(queue.onDequeue(queue.peek(), now()), DequeueAction::Dispatch);
  EXPECT_EQ(queue.pop().value(), 1);
  EXPECT_FALSE(queue.isOverloaded());

  advance(100ms);
  EXPECT_EQ(queue.onDequeue(queue.peek(), now()), DequeueAction::Dispatch);
  EXPECT_TRUE(queue.isOverloaded());
  EXPECT_EQ(queue.pop().value(), 2);

  advance(1ms);
  EXPECT_EQ(queue.onDequeue(queue.peek(), now()), DequeueAction::Drop);
  EXPECT_EQ(queue.pop().value(), 3);
  EXPECT_TRUE(queue.empty());
  EXPECT_FALSE(queue.isOverloaded());

  queue.add(fourth, {now()});
  EXPECT_EQ(queue.onDequeue(queue.peek(), now()), DequeueAction::Dispatch);
  EXPECT_EQ(queue.pop().value(), 4);
}

TEST_F(CodelQueueTest, DelegatesTraversalAndUnderlyingDrop) {
  CodelQueue<TestItem> queue(std::make_unique<AlwaysDropFifoQueue>(), 5ms, 100ms);
  TestItem item(1);
  queue.add(item, {now()});

  EXPECT_EQ(queue.size(), 1);
  const CodelQueue<TestItem>& const_queue = queue;
  EXPECT_EQ(const_queue.peek().value(), 1);

  int visited = 0;
  queue.forEach([&visited](TestItem& visited_item) {
    EXPECT_EQ(visited_item.value(), 1);
    ++visited;
    return true;
  });
  EXPECT_EQ(visited, 1);

  advance(200ms);
  EXPECT_EQ(queue.onDequeue(queue.peek(), now()), DequeueAction::Drop);
  EXPECT_FALSE(queue.isOverloaded());
  EXPECT_EQ(queue.pop().value(), 1);
}

TEST_F(CodelQueueTest, DelayBoundaryAndRecovery) {
  CodelController controller(5ms, 100ms);
  const MonotonicTime start = now();
  advance(5ms);
  EXPECT_FALSE(controller.shouldDrop(start, now()));
  advance(100ms);
  EXPECT_FALSE(controller.shouldDrop(start, now()));
  EXPECT_FALSE(controller.isOverloaded());

  const MonotonicTime delayed = now();
  advance(11ms);
  EXPECT_FALSE(controller.shouldDrop(delayed, now()));
  advance(100ms);
  EXPECT_FALSE(controller.shouldDrop(delayed, now()));
  EXPECT_TRUE(controller.isOverloaded());

  const MonotonicTime exactly_twice_target = now() - 10ms;
  EXPECT_FALSE(controller.shouldDrop(exactly_twice_target, now()));
  const MonotonicTime above_twice_target = now() - 11ms;
  EXPECT_TRUE(controller.shouldDrop(above_twice_target, now()));

  const MonotonicTime recent = now();
  EXPECT_FALSE(controller.shouldDrop(recent, now()));
  advance(100ms);
  EXPECT_FALSE(controller.shouldDrop(now(), now()));
  EXPECT_FALSE(controller.isOverloaded());
}

TEST_F(CodelQueueTest, WrappedAdaptiveLifoSelectsNewestItemAndSupportsRemoval) {
  CodelQueue<TestItem> queue(std::make_unique<AdaptiveLifoQueue<TestItem>>(2, 0), 5ms, 100ms);
  TestItem first(1);
  TestItem second(2);
  const MonotonicTime start = now();
  queue.add(first, {start});
  queue.add(second, {start});

  EXPECT_TRUE(queue.isOverloaded());
  EXPECT_EQ(queue.peek().value(), 2);
  queue.remove(second);
  EXPECT_TRUE(queue.isOverloaded());
  EXPECT_EQ(queue.peek().value(), 1);
  EXPECT_EQ(queue.pop().value(), 1);
  EXPECT_TRUE(queue.empty());
  EXPECT_FALSE(queue.isOverloaded());
}

TEST(CodelQueueFactoryTest, CreatesDefaultFifoQueue) {
  CodelQueueFactory<TestItem> factory;
  CodelQueuePolicyConfig config;
  auto result = factory.createQueuePolicy(config, "test_prefix",
                                          ProtobufMessage::getStrictValidationVisitor());
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(factory.name(), "envoy.queue_policy.codel");
  TestItem first(1);
  TestItem second(2);
  result.value()->add(first, {MonotonicTime{}});
  result.value()->add(second, {MonotonicTime{}});
  EXPECT_EQ(result.value()->pop().value(), 1);
  EXPECT_EQ(result.value()->pop().value(), 2);
}

TEST(CodelQueueFactoryTest, RejectsInvalidInterval) {
  CodelQueueFactory<TestItem> factory;
  CodelQueuePolicyConfig config;
  config.mutable_target_delay()->set_nanos(10000000);
  config.mutable_interval()->set_nanos(10000000);
  auto result = factory.createQueuePolicy(config, "test_prefix",
                                          ProtobufMessage::getStrictValidationVisitor());
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(CodelQueueFactoryTest, RejectsUnknownUnderlyingPolicy) {
  CodelQueueFactory<TestItem> factory;
  CodelQueuePolicyConfig config;
  config.mutable_underlying_queue_policy()->set_name("envoy.queue_policy.unknown");
  EXPECT_TRUE(config.mutable_underlying_queue_policy()->mutable_typed_config()->PackFrom(
      CodelQueuePolicyConfig{}));
  auto result = factory.createQueuePolicy(config, "test_prefix",
                                          ProtobufMessage::getStrictValidationVisitor());
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(CodelQueueFactoryTest, WrapsConfiguredFifoPolicy) {
  CodelQueueFactory<ConnectionPool::PendingStream> factory;
  CodelQueuePolicyConfig config;
  config.mutable_target_delay()->set_nanos(3000000);
  config.mutable_interval()->set_nanos(50000000);
  config.mutable_underlying_queue_policy()->set_name("envoy.queue_policy.fifo");
  EXPECT_TRUE(config.mutable_underlying_queue_policy()->mutable_typed_config()->PackFrom(
      envoy::extensions::queue_policy::fifo::v3::FifoQueuePolicyConfig{}));

  auto result = factory.createQueuePolicy(config, "test_prefix",
                                          ProtobufMessage::getStrictValidationVisitor());
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_NE(result.value(), nullptr);
}

TEST(CodelQueueFactoryTest, RejectsNestedCodelPolicy) {
  CodelQueueFactory<ConnectionPool::PendingStream> factory;
  CodelQueuePolicyConfig config;
  config.mutable_underlying_queue_policy()->set_name("envoy.queue_policy.codel");
  EXPECT_TRUE(config.mutable_underlying_queue_policy()->mutable_typed_config()->PackFrom(
      CodelQueuePolicyConfig{}));

  auto result = factory.createQueuePolicy(config, "test_prefix",
                                          ProtobufMessage::getStrictValidationVisitor());
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(), "CoDel queue policies cannot be nested");
}

} // namespace
} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
