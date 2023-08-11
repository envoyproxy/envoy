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
  Buffer::OwnedImpl received_data;
  Buffer::OwnedImpl out_data;
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(0, queue.bytesEnqueued());
  EXPECT_FALSE(queue.pop(false, received_data, out_data));
}

TEST(StateTest, BasicQueue) {
  ChunkQueue queue;
  Buffer::OwnedImpl received_data;
  Buffer::OwnedImpl out_data;
  Buffer::OwnedImpl data1("Hello");

  EXPECT_EQ(received_data.length(), 0);
  queue.push(data1, false, false, received_data);
  EXPECT_EQ(received_data.toString(), "Hello");
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(5, queue.bytesEnqueued());
  auto popped = queue.pop(false, received_data, out_data);
  EXPECT_EQ(out_data.toString(), "Hello");
  EXPECT_FALSE((*popped)->end_stream);
  EXPECT_FALSE((*popped)->delivered);
  EXPECT_EQ((*popped)->buffer_length, 5);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(0, queue.bytesEnqueued());
}

TEST(StateTest, EnqueueDequeue) {
  ChunkQueue queue;
  Buffer::OwnedImpl received_data;
  Buffer::OwnedImpl out_data;
  Buffer::OwnedImpl data1("Hello");
  queue.push(data1, false, false, received_data);
  Buffer::OwnedImpl data2(", World!");
  queue.push(data2, false, false, received_data);
  EXPECT_EQ(received_data.toString(), "Hello, World!");
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(13, queue.bytesEnqueued());
  auto popped = queue.pop(false, received_data, out_data);
  EXPECT_EQ(out_data.toString(), "Hello");
  EXPECT_FALSE((*popped)->end_stream);
  EXPECT_FALSE((*popped)->delivered);
  EXPECT_EQ((*popped)->buffer_length, 5);
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(8, queue.bytesEnqueued());
  Buffer::OwnedImpl data3("Bye");
  queue.push(data3, true, false, received_data);
  EXPECT_EQ(11, queue.bytesEnqueued());

  out_data.drain(out_data.length());
  popped = queue.pop(false, received_data, out_data);
  EXPECT_EQ(out_data.toString(), ", World!");
  EXPECT_FALSE((*popped)->end_stream);
  EXPECT_FALSE((*popped)->delivered);
  EXPECT_EQ((*popped)->buffer_length, 8);
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(3, queue.bytesEnqueued());

  out_data.drain(out_data.length());
  popped = queue.pop(false, received_data, out_data);
  EXPECT_EQ(out_data.toString(), "Bye");
  EXPECT_TRUE((*popped)->end_stream);
  EXPECT_FALSE((*popped)->delivered);
  EXPECT_EQ((*popped)->buffer_length, 3);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(0, queue.bytesEnqueued());

  out_data.drain(out_data.length());
  popped = queue.pop(false, received_data, out_data);
  EXPECT_FALSE(popped);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(0, queue.bytesEnqueued());
}

TEST(StateTest, ConsolidateThree) {
  ChunkQueue queue;
  Buffer::OwnedImpl received_data;
  Buffer::OwnedImpl out_data;
  Buffer::OwnedImpl data1("Hello");
  queue.push(data1, false, false, received_data);
  Buffer::OwnedImpl data2(", ");
  queue.push(data2, false, false, received_data);
  Buffer::OwnedImpl data3("World!");
  queue.push(data3, false, false, received_data);
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(13, queue.bytesEnqueued());
  const auto& chunk = queue.consolidate(received_data, out_data);
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(13, queue.bytesEnqueued());
  EXPECT_EQ(out_data.toString(), "Hello, World!");
  EXPECT_FALSE(chunk.end_stream);
  EXPECT_TRUE(chunk.delivered);
  EXPECT_EQ(chunk.buffer_length, 13);
}

TEST(StateTest, ConsolidateOne) {
  ChunkQueue queue;
  Buffer::OwnedImpl received_data;
  Buffer::OwnedImpl out_data;
  Buffer::OwnedImpl data1("Hello");
  queue.push(data1, false, false, received_data);
  const auto& chunk = queue.consolidate(received_data, out_data);
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(5, queue.bytesEnqueued());
  EXPECT_EQ(out_data.toString(), "Hello");
  EXPECT_FALSE(chunk.end_stream);
  EXPECT_TRUE(chunk.delivered);
  EXPECT_EQ(chunk.buffer_length, 5);
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
