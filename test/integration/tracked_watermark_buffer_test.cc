#include <chrono>
#include <memory>

#include "envoy/buffer/buffer.h"

#include "source/common/buffer/buffer_impl.h"

#include "test/integration/tracked_watermark_buffer.h"
#include "test/mocks/common.h"
#include "test/mocks/http/stream_reset_handler.h"
#include "test/test_common/thread_factory_for_test.h"

#include "gtest/gtest.h"

using testing::InSequence;
using testing::Pair;

namespace Envoy {
namespace Buffer {
namespace {

class TrackedWatermarkBufferTest : public testing::Test {
public:
  TrackedWatermarkBufferFactory factory_{absl::bit_width(4096u)};
  Http::MockStreamResetHandler mock_reset_handler_;
};

TEST_F(TrackedWatermarkBufferTest, WatermarkFunctions) {
  InSequence s;

  ReadyWatcher low_watermark;
  ReadyWatcher high_watermark;
  ReadyWatcher overflow_watermark;
  ReadyWatcher now;

  auto buffer =
      factory_.createBuffer([&]() { low_watermark.ready(); }, [&]() { high_watermark.ready(); },
                            [&]() { overflow_watermark.ready(); });
  // Test highWatermarkRange
  EXPECT_THAT(factory_.highWatermarkRange(), Pair(0, 0));

  buffer->setWatermarks(100, 2);
  EXPECT_THAT(factory_.highWatermarkRange(), Pair(100, 100));

  auto buffer2 = factory_.createBuffer([]() {}, []() {}, []() {});
  EXPECT_THAT(factory_.highWatermarkRange(), Pair(100, 0));

  buffer2->setWatermarks(200, 2);
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
}

TEST_F(TrackedWatermarkBufferTest, BufferSizes) {
  auto buffer = factory_.createBuffer([]() {}, []() {}, []() {});
  buffer->setWatermarks(100);
  auto buffer2 = factory_.createBuffer([]() {}, []() {}, []() {});
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
  auto buffer1 = factory_.createBuffer([]() {}, []() {}, []() {});
  auto buffer2 = factory_.createBuffer([]() {}, []() {}, []() {});
  auto buffer3 = factory_.createBuffer([]() {}, []() {}, []() {});

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

TEST_F(TrackedWatermarkBufferTest, TracksNumberOfBuffersActivelyBound) {
  auto buffer1 = factory_.createBuffer([]() {}, []() {}, []() {});
  auto buffer2 = factory_.createBuffer([]() {}, []() {}, []() {});
  auto buffer3 = factory_.createBuffer([]() {}, []() {}, []() {});
  auto account = factory_.createAccount(mock_reset_handler_);
  ASSERT_TRUE(factory_.waitUntilExpectedNumberOfAccountsAndBoundBuffers(0, 0));

  buffer1->bindAccount(account);
  EXPECT_TRUE(factory_.waitUntilExpectedNumberOfAccountsAndBoundBuffers(1, 1));
  buffer2->bindAccount(account);
  EXPECT_TRUE(factory_.waitUntilExpectedNumberOfAccountsAndBoundBuffers(1, 2));
  buffer3->bindAccount(account);
  EXPECT_TRUE(factory_.waitUntilExpectedNumberOfAccountsAndBoundBuffers(1, 3));

  // Release test and account access to shared_this.
  account->clearDownstream();
  account.reset();

  buffer3.reset();
  EXPECT_TRUE(factory_.waitUntilExpectedNumberOfAccountsAndBoundBuffers(1, 2));
  buffer2.reset();
  EXPECT_TRUE(factory_.waitUntilExpectedNumberOfAccountsAndBoundBuffers(1, 1));
  buffer1.reset();
  EXPECT_TRUE(factory_.waitUntilExpectedNumberOfAccountsAndBoundBuffers(0, 0));
}

TEST_F(TrackedWatermarkBufferTest, TracksNumberOfAccountsActive) {
  auto buffer1 = factory_.createBuffer([]() {}, []() {}, []() {});
  auto buffer2 = factory_.createBuffer([]() {}, []() {}, []() {});
  auto buffer3 = factory_.createBuffer([]() {}, []() {}, []() {});
  auto account1 = factory_.createAccount(mock_reset_handler_);
  ASSERT_TRUE(factory_.waitUntilExpectedNumberOfAccountsAndBoundBuffers(0, 0));

  buffer1->bindAccount(account1);
  EXPECT_TRUE(factory_.waitUntilExpectedNumberOfAccountsAndBoundBuffers(1, 1));
  buffer2->bindAccount(account1);
  EXPECT_TRUE(factory_.waitUntilExpectedNumberOfAccountsAndBoundBuffers(1, 2));

  // Release test and account access to shared_this.
  account1->clearDownstream();
  account1.reset();

  auto account2 = factory_.createAccount(mock_reset_handler_);
  buffer3->bindAccount(account2);
  EXPECT_TRUE(factory_.waitUntilExpectedNumberOfAccountsAndBoundBuffers(2, 3));

  buffer2.reset();
  EXPECT_TRUE(factory_.waitUntilExpectedNumberOfAccountsAndBoundBuffers(2, 2));
  buffer1.reset();
  EXPECT_TRUE(factory_.waitUntilExpectedNumberOfAccountsAndBoundBuffers(1, 1));

  // Release test and account access to shared_this.
  account2->clearDownstream();
  account2.reset();

  buffer3.reset();
  EXPECT_TRUE(factory_.waitUntilExpectedNumberOfAccountsAndBoundBuffers(0, 0));
}

TEST_F(TrackedWatermarkBufferTest, WaitForExpectedAccountBalanceShouldReturnTrueWhenConditionsMet) {
  auto buffer1 = factory_.createBuffer([]() {}, []() {}, []() {});
  auto buffer2 = factory_.createBuffer([]() {}, []() {}, []() {});
  auto account1 = factory_.createAccount(mock_reset_handler_);
  auto account2 = factory_.createAccount(mock_reset_handler_);
  buffer1->bindAccount(account1);
  buffer2->bindAccount(account2);

  factory_.setExpectedAccountBalance(4096, 2);

  buffer1->add("Need to wait on the other buffer to get data.");
  EXPECT_FALSE(factory_.waitForExpectedAccountBalanceWithTimeout(std::chrono::seconds(0)));

  buffer2->add("Now we have expected balances!");
  EXPECT_TRUE(factory_.waitForExpectedAccountBalanceWithTimeout(std::chrono::seconds(0)));

  account1->clearDownstream();
  account2->clearDownstream();
}

} // namespace
} // namespace Buffer
} // namespace Envoy
