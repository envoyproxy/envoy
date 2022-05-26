#include "source/extensions/filters/http/ext_proc/processor_state.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

TEST(StateTest, EmptyQueue) {
  ChunkQueue queue;
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(0, queue.bytesEnqueued());
  EXPECT_FALSE(queue.pop(false));
}

TEST(StateTest, BasicQueue) {
  ChunkQueue queue;
  Buffer::OwnedImpl data1("Hello");
  queue.push(data1, false, false);
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(5, queue.bytesEnqueued());
  auto popped = queue.pop(false);
  EXPECT_EQ((*popped)->data.toString(), "Hello");
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(0, queue.bytesEnqueued());
}

TEST(StateTest, EnqueueDequeue) {
  ChunkQueue queue;
  Buffer::OwnedImpl data1("Hello");
  queue.push(data1, false, false);
  Buffer::OwnedImpl data2(", World!");
  queue.push(data2, false, false);
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(13, queue.bytesEnqueued());
  auto popped = queue.pop(false);
  EXPECT_EQ((*popped)->data.toString(), "Hello");
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(8, queue.bytesEnqueued());
  Buffer::OwnedImpl data3("Bye");
  EXPECT_FALSE(queue.empty());
  queue.push(data3, false, false);
  EXPECT_EQ(11, queue.bytesEnqueued());
  popped = queue.pop(false);
  EXPECT_EQ((*popped)->data.toString(), ", World!");
  popped = queue.pop(false);
  EXPECT_EQ((*popped)->data.toString(), "Bye");
  popped = queue.pop(false);
  EXPECT_FALSE(popped);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(0, queue.bytesEnqueued());
}

TEST(StateTest, ConsolidateThree) {
  ChunkQueue queue;
  Buffer::OwnedImpl data1("Hello");
  queue.push(data1, false, false);
  Buffer::OwnedImpl data2(", ");
  queue.push(data2, false, false);
  Buffer::OwnedImpl data3("World!");
  queue.push(data3, false, false);
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(13, queue.bytesEnqueued());
  const auto& chunk = queue.consolidate(true);
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(13, queue.bytesEnqueued());
  EXPECT_EQ(chunk.data.toString(), "Hello, World!");
  EXPECT_TRUE(chunk.delivered);
}

TEST(StateTest, ConsolidateOne) {
  ChunkQueue queue;
  Buffer::OwnedImpl data1("Hello");
  queue.push(data1, false, false);
  const auto& chunk = queue.consolidate(true);
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(5, queue.bytesEnqueued());
  EXPECT_EQ(chunk.data.toString(), "Hello");
  EXPECT_TRUE(chunk.delivered);
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
