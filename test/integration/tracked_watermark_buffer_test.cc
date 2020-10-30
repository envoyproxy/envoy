#include "test/integration/tracked_watermark_buffer.h"
#include "test/mocks/common.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/thread_factory_for_test.h"

#include "gtest/gtest.h"

using testing::InSequence;
using testing::Pair;

namespace Envoy {
namespace Buffer {
namespace {

class TrackedWatermarkBufferTest : public testing::Test {
public:
  TrackedWatermarkBufferFactory factory_;
};

TEST_F(TrackedWatermarkBufferTest, WatermarkFunctions) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues({{"envoy.buffer.overflow_multiplier", "2"}});

  InSequence s;

  ReadyWatcher low_watermark;
  ReadyWatcher high_watermark;
  ReadyWatcher overflow_watermark;
  ReadyWatcher now;

  auto buffer = factory_.create([&]() { low_watermark.ready(); }, [&]() { high_watermark.ready(); },
                                [&]() { overflow_watermark.ready(); });
  // Test highWatermarkRange
  EXPECT_THAT(factory_.highWatermarkRange(), Pair(0, 0));

  buffer->setWatermarks(100);
  EXPECT_THAT(factory_.highWatermarkRange(), Pair(100, 100));

  auto buffer2 = factory_.create([]() {}, []() {}, []() {});
  EXPECT_THAT(factory_.highWatermarkRange(), Pair(100, 0));

  buffer2->setWatermarks(200);
  EXPECT_THAT(factory_.highWatermarkRange(), Pair(100, 200));

  // Verify that the buffer watermark functions are called.
  buffer->add(std::string(100, 'a'));
  EXPECT_CALL(high_watermark, ready());
  buffer->add("b");
  EXPECT_CALL(overflow_watermark, ready());
  EXPECT_CALL(now, ready());
  buffer->add(std::string(100, 'c'));
  now.ready();
  buffer->add(std::string(100, 'd'));
  buffer->drain(250);
  EXPECT_CALL(low_watermark, ready());
  buffer->drain(1);

  // Verify overflow stats.
  EXPECT_EQ(3.01, std::get<0>(factory_.maxOverflowRatio()));
  EXPECT_EQ(100, std::get<1>(factory_.maxOverflowRatio()));
  EXPECT_EQ(301, std::get<2>(factory_.maxOverflowRatio()));
}

TEST_F(TrackedWatermarkBufferTest, BufferSizes) {
  auto buffer = factory_.create([]() {}, []() {}, []() {});
  buffer->setWatermarks(100);
  auto buffer2 = factory_.create([]() {}, []() {}, []() {});

  EXPECT_EQ(2, factory_.numBuffersCreated());
  EXPECT_EQ(2, factory_.numBuffersActive());

  // Add some bytes to the buffers, and verify max and sum(max).
  buffer->add("abcde");
  buffer2->add("a");

  EXPECT_EQ(5, factory_.maxBufferSize());
  EXPECT_EQ(6, factory_.sumMaxBufferSizes());
  EXPECT_EQ(6, factory_.totalBytesBuffered());

  // Add more bytes and drain the buffer. Verify that max is latched.
  buffer->add(std::string(1000, 'a'));
  EXPECT_TRUE(buffer->highWatermarkTriggered());
  EXPECT_EQ(1006, factory_.totalBytesBuffered());
  buffer->drain(1005);
  EXPECT_EQ(0, buffer->length());
  EXPECT_FALSE(buffer->highWatermarkTriggered());
  EXPECT_EQ(1005, factory_.maxBufferSize());
  EXPECT_EQ(1006, factory_.sumMaxBufferSizes());
  EXPECT_EQ(1, factory_.totalBytesBuffered());

  buffer2->add("a");
  EXPECT_EQ(1005, factory_.maxBufferSize());
  EXPECT_EQ(1007, factory_.sumMaxBufferSizes());
  EXPECT_EQ(2, factory_.totalBytesBuffered());

  // Verify cleanup tracking.
  buffer.reset();
  EXPECT_EQ(2, factory_.numBuffersCreated());
  EXPECT_EQ(1, factory_.numBuffersActive());
  buffer2.reset();
  EXPECT_EQ(2, factory_.numBuffersCreated());
  EXPECT_EQ(0, factory_.numBuffersActive());
  // Bytes in deleted buffers are removed from the total.
  EXPECT_EQ(0, factory_.totalBytesBuffered());

  // Max sizes are remembered even after buffers are deleted.
  EXPECT_EQ(1005, factory_.maxBufferSize());
  EXPECT_EQ(1007, factory_.sumMaxBufferSizes());
}

TEST_F(TrackedWatermarkBufferTest, WaitUntilTotalBufferedExceeds) {
  auto buffer1 = factory_.create([]() {}, []() {}, []() {});
  auto buffer2 = factory_.create([]() {}, []() {}, []() {});
  auto buffer3 = factory_.create([]() {}, []() {}, []() {});

  auto thread1 = Thread::threadFactoryForTest().createThread([&]() { buffer1->add("a"); });
  auto thread2 = Thread::threadFactoryForTest().createThread([&]() { buffer2->add("b"); });
  auto thread3 = Thread::threadFactoryForTest().createThread([&]() { buffer3->add("c"); });

  factory_.waitUntilTotalBufferedExceeds(2, std::chrono::milliseconds(10000));
  thread1->join();
  thread2->join();
  thread3->join();

  EXPECT_EQ(3, factory_.totalBytesBuffered());
  EXPECT_EQ(1, factory_.maxBufferSize());
}

} // namespace
} // namespace Buffer
} // namespace Envoy
