#include <cstdint>
#include <memory>
#include <type_traits>
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

  static_assert(std::is_same_v<decltype(queue.peek()), FifoQueueItemType&>);
  const FifoQueue<FifoQueueItemType>& const_queue = queue;
  static_assert(std::is_same_v<decltype(const_queue.peek()), const FifoQueueItemType&>);
  EXPECT_EQ(const_queue.peek().value(), 11);

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

  static_assert(std::is_same_v<decltype(queue.pop()), FifoQueueItemType&>);
  EXPECT_EQ(queue.pop().value(), 11);
  EXPECT_EQ(queue.peek().value(), 7);
  queue.remove(third);
  EXPECT_TRUE(queue.empty());
}

// The callback may remove and destroy items that forEach() has not visited yet, as the connection
// pool does when attaching one pending stream cancels another. Heap-allocated items let ASAN catch
// any access to a destroyed item.
TEST(FifoQueueTest, ForEachCallbackRemovesUnvisitedItems) {
  std::vector<std::unique_ptr<FifoQueueItemType>> items;
  FifoQueue<FifoQueueItemType> queue;
  for (int value = 1; value <= 5; ++value) {
    items.push_back(std::make_unique<FifoQueueItemType>(value));
    queue.add(*items.back(), {MonotonicTime{}});
  }

  std::vector<int> visited;
  queue.forEach([&](FifoQueueItemType& item) -> bool {
    visited.push_back(item.value());
    if (item.value() == 1) {
      // Remove the visited item, the next item and the last item, then destroy the latter two.
      queue.remove(item);
      queue.remove(*items[1]);
      queue.remove(*items[4]);
      items[1].reset();
      items[4].reset();
    }
    return true;
  });
  EXPECT_EQ(visited, (std::vector<int>{1, 3, 4}));
  EXPECT_EQ(queue.size(), 2);
  EXPECT_EQ(queue.pop().value(), 3);
  EXPECT_EQ(queue.pop().value(), 4);
  EXPECT_TRUE(queue.empty());
}

TEST(FifoQueueTest, ForEachCallbackRemovesAllItems) {
  std::vector<std::unique_ptr<FifoQueueItemType>> items;
  FifoQueue<FifoQueueItemType> queue;
  for (int value = 1; value <= 4; ++value) {
    items.push_back(std::make_unique<FifoQueueItemType>(value));
    queue.add(*items.back(), {MonotonicTime{}});
  }

  std::vector<int> visited;
  queue.forEach([&](FifoQueueItemType& item) -> bool {
    visited.push_back(item.value());
    // Mirrors a connection close purging every pending stream from inside the callback.
    while (!queue.empty()) {
      queue.pop();
    }
    for (size_t i = 1; i < items.size(); ++i) {
      items[i].reset();
    }
    return true;
  });
  EXPECT_EQ(visited, (std::vector<int>{1}));
  EXPECT_TRUE(queue.empty());
}

TEST(FifoQueueTest, ForEachDoesNotVisitItemsAddedByCallback) {
  FifoQueueItemType first(1);
  FifoQueueItemType second(2);
  FifoQueueItemType added(3);
  FifoQueue<FifoQueueItemType> queue;
  queue.add(first, {MonotonicTime{}});
  queue.add(second, {MonotonicTime{}});

  std::vector<int> visited;
  queue.forEach([&](FifoQueueItemType& item) -> bool {
    visited.push_back(item.value());
    if (item.value() == 1) {
      queue.add(added, {MonotonicTime{}});
    }
    return true;
  });
  EXPECT_EQ(visited, (std::vector<int>{1, 2}));
  EXPECT_EQ(queue.size(), 3);
}

TEST(FifoQueueTest, NestedForEachRemovingItemsKeepsOuterTraversalValid) {
  std::vector<std::unique_ptr<FifoQueueItemType>> items;
  FifoQueue<FifoQueueItemType> queue;
  for (int value = 1; value <= 4; ++value) {
    items.push_back(std::make_unique<FifoQueueItemType>(value));
    queue.add(*items.back(), {MonotonicTime{}});
  }

  std::vector<int> outer;
  std::vector<int> inner;
  queue.forEach([&](FifoQueueItemType& item) -> bool {
    outer.push_back(item.value());
    if (item.value() == 1) {
      queue.forEach([&](FifoQueueItemType& nested) -> bool {
        inner.push_back(nested.value());
        if (nested.value() == 2) {
          queue.remove(nested);
          items[1].reset();
        }
        return true;
      });
    }
    return true;
  });
  EXPECT_EQ(inner, (std::vector<int>{1, 2, 3, 4}));
  EXPECT_EQ(outer, (std::vector<int>{1, 3, 4}));
  EXPECT_EQ(queue.size(), 3);
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
