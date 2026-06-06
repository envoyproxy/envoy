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
  Buffer::OwnedImpl out_data;
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(0, queue.bytesEnqueued());
  EXPECT_FALSE(queue.pop(out_data));
}

TEST(StateTest, BasicQueue) {
  ChunkQueue queue;
  Buffer::OwnedImpl out_data;
  Buffer::OwnedImpl data1("Hello");

  EXPECT_EQ(queue.receivedData().length(), 0);
  queue.push(data1, false);
  EXPECT_EQ(queue.receivedData().toString(), "Hello");
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(5, queue.bytesEnqueued());
  auto popped = queue.pop(out_data);
  EXPECT_EQ(out_data.toString(), "Hello");
  EXPECT_FALSE(popped->end_stream);
  EXPECT_EQ(popped->length, 5);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(0, queue.bytesEnqueued());
  EXPECT_EQ(queue.receivedData().length(), 0);
}

TEST(StateTest, OkToPushEmptyDataToQueue) {
  ChunkQueue queue;
  Buffer::OwnedImpl out_data;
  Buffer::OwnedImpl data1("");

  EXPECT_EQ(queue.receivedData().length(), 0);
  queue.push(data1, true);
  EXPECT_EQ(queue.receivedData().toString(), "");
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(0, queue.bytesEnqueued());
  auto popped = queue.pop(out_data);
  EXPECT_EQ(out_data.toString(), "");
  EXPECT_TRUE(popped->end_stream);
  EXPECT_EQ(popped->length, 0);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(0, queue.bytesEnqueued());
  EXPECT_EQ(queue.receivedData().length(), 0);

  Buffer::OwnedImpl data2("Hello");
  queue.push(data2, false);
  EXPECT_EQ(queue.receivedData().toString(), "Hello");
  queue.push(data1, true);
  EXPECT_EQ(queue.receivedData().toString(), "Hello");

  out_data.drain(out_data.length());
  popped = queue.pop(out_data);
  EXPECT_EQ(out_data.toString(), "Hello");
  EXPECT_FALSE(popped->end_stream);
  EXPECT_EQ(popped->length, 5);
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(0, queue.bytesEnqueued());
  EXPECT_EQ(queue.receivedData().length(), 0);

  out_data.drain(out_data.length());
  popped = queue.pop(out_data);
  EXPECT_EQ(out_data.toString(), "");
  EXPECT_TRUE(popped->end_stream);
  EXPECT_EQ(popped->length, 0);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(0, queue.bytesEnqueued());
  EXPECT_EQ(queue.receivedData().length(), 0);
}

TEST(StateTest, EnqueueDequeue) {
  ChunkQueue queue;
  Buffer::OwnedImpl out_data;
  Buffer::OwnedImpl data1("Hello");
  queue.push(data1, false);
  Buffer::OwnedImpl data2(", World!");
  queue.push(data2, false);
  EXPECT_EQ(queue.receivedData().toString(), "Hello, World!");
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(13, queue.bytesEnqueued());
  auto popped = queue.pop(out_data);
  EXPECT_EQ(out_data.toString(), "Hello");
  EXPECT_FALSE(popped->end_stream);
  EXPECT_EQ(popped->length, 5);
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(8, queue.bytesEnqueued());
  EXPECT_EQ(queue.receivedData().length(), 8);
  EXPECT_EQ(queue.receivedData().toString(), ", World!");
  Buffer::OwnedImpl data3("Bye");
  queue.push(data3, true);
  EXPECT_EQ(11, queue.bytesEnqueued());
  EXPECT_EQ(queue.receivedData().toString(), ", World!Bye");

  out_data.drain(out_data.length());
  popped = queue.pop(out_data);
  EXPECT_EQ(out_data.toString(), ", World!");
  EXPECT_FALSE(popped->end_stream);
  EXPECT_EQ(popped->length, 8);
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(3, queue.bytesEnqueued());
  EXPECT_EQ(queue.receivedData().toString(), "Bye");

  out_data.drain(out_data.length());
  popped = queue.pop(out_data);
  EXPECT_EQ(out_data.toString(), "Bye");
  EXPECT_TRUE(popped->end_stream);
  EXPECT_EQ(popped->length, 3);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(0, queue.bytesEnqueued());
  EXPECT_EQ(queue.receivedData().length(), 0);

  out_data.drain(out_data.length());
  popped = queue.pop(out_data);
  EXPECT_FALSE(popped);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(0, queue.bytesEnqueued());
}

TEST(StateTest, ConsolidateThree) {
  ChunkQueue queue;
  Buffer::OwnedImpl data1("Hello");
  queue.push(data1, false);
  Buffer::OwnedImpl data2(", ");
  queue.push(data2, false);
  Buffer::OwnedImpl data3("World!");
  queue.push(data3, false);
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(13, queue.bytesEnqueued());
  const auto& chunk = queue.consolidate();
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(13, queue.bytesEnqueued());
  EXPECT_EQ(queue.receivedData().toString(), "Hello, World!");
  EXPECT_FALSE(chunk.end_stream);
  EXPECT_EQ(chunk.length, 13);
}

TEST(StateTest, ConsolidateOne) {
  ChunkQueue queue;
  Buffer::OwnedImpl data1("Hello");
  queue.push(data1, false);
  const auto& chunk = queue.consolidate();
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(5, queue.bytesEnqueued());
  EXPECT_EQ(queue.receivedData().toString(), "Hello");
  EXPECT_FALSE(chunk.end_stream);
  EXPECT_EQ(chunk.length, 5);
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
