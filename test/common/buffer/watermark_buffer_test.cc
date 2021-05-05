#include <array>

#include "common/api/os_sys_calls_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/buffer/watermark_buffer.h"
#include "common/network/io_socket_handle_impl.h"

#include "test/common/buffer/utility.h"
#include "test/test_common/test_runtime.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Buffer {
namespace {

const char TEN_BYTES[] = "0123456789";

class WatermarkBufferTest : public testing::Test {
public:
  WatermarkBufferTest() { buffer_.setWatermarks(5, 10); }

  Buffer::WatermarkBuffer buffer_{[&]() -> void { ++times_low_watermark_called_; },
                                  [&]() -> void { ++times_high_watermark_called_; },
                                  [&]() -> void { ++times_overflow_watermark_called_; }};
  uint32_t times_low_watermark_called_{0};
  uint32_t times_high_watermark_called_{0};
  uint32_t times_overflow_watermark_called_{0};
};

TEST_F(WatermarkBufferTest, TestWatermark) { ASSERT_EQ(10, buffer_.highWatermark()); }

TEST_F(WatermarkBufferTest, CopyOut) {
  buffer_.add("hello world");
  std::array<char, 5> out;
  buffer_.copyOut(0, out.size(), out.data());
  EXPECT_EQ(std::string(out.data(), out.size()), "hello");

  buffer_.copyOut(6, out.size(), out.data());
  EXPECT_EQ(std::string(out.data(), out.size()), "world");

  // Copy out zero bytes.
  buffer_.copyOut(4, 0, out.data());
}

TEST_F(WatermarkBufferTest, AddChar) {
  buffer_.add(TEN_BYTES, 10);
  EXPECT_EQ(0, times_high_watermark_called_);
  buffer_.add("a", 1);
  EXPECT_EQ(1, times_high_watermark_called_);
  EXPECT_EQ(11, buffer_.length());
}

TEST_F(WatermarkBufferTest, AddString) {
  buffer_.add(std::string(TEN_BYTES));
  EXPECT_EQ(0, times_high_watermark_called_);
  buffer_.add(std::string("a"));
  EXPECT_EQ(1, times_high_watermark_called_);
  EXPECT_EQ(11, buffer_.length());
}

TEST_F(WatermarkBufferTest, AddBuffer) {
  OwnedImpl first(TEN_BYTES);
  buffer_.add(first);
  EXPECT_EQ(0, times_high_watermark_called_);
  OwnedImpl second("a");
  buffer_.add(second);
  EXPECT_EQ(1, times_high_watermark_called_);
  EXPECT_EQ(11, buffer_.length());
}

TEST_F(WatermarkBufferTest, Prepend) {
  std::string suffix = "World!", prefix = "Hello, ";

  buffer_.add(suffix);
  EXPECT_EQ(0, times_high_watermark_called_);
  buffer_.prepend(prefix);
  EXPECT_EQ(1, times_high_watermark_called_);
  EXPECT_EQ(suffix.size() + prefix.size(), buffer_.length());
}

TEST_F(WatermarkBufferTest, PrependToEmptyBuffer) {
  std::string suffix = "World!", prefix = "Hello, ";

  buffer_.prepend(suffix);
  EXPECT_EQ(0, times_high_watermark_called_);
  EXPECT_EQ(suffix.size(), buffer_.length());

  buffer_.prepend(prefix.data());
  EXPECT_EQ(1, times_high_watermark_called_);
  EXPECT_EQ(suffix.size() + prefix.size(), buffer_.length());

  buffer_.prepend("");
  EXPECT_EQ(1, times_high_watermark_called_);
  EXPECT_EQ(suffix.size() + prefix.size(), buffer_.length());
}

TEST_F(WatermarkBufferTest, PrependBuffer) {
  std::string suffix = "World!", prefix = "Hello, ";

  uint32_t prefix_buffer_low_watermark_hits{0};
  uint32_t prefix_buffer_high_watermark_hits{0};
  uint32_t prefix_buffer_overflow_watermark_hits{0};
  WatermarkBuffer prefixBuffer{[&]() -> void { ++prefix_buffer_low_watermark_hits; },
                               [&]() -> void { ++prefix_buffer_high_watermark_hits; },
                               [&]() -> void { ++prefix_buffer_overflow_watermark_hits; }};
  prefixBuffer.setWatermarks(5, 10);
  prefixBuffer.add(prefix);
  prefixBuffer.add(suffix);

  EXPECT_EQ(1, prefix_buffer_high_watermark_hits);
  buffer_.prepend(prefixBuffer);

  EXPECT_EQ(1, times_high_watermark_called_);
  EXPECT_EQ(1, prefix_buffer_low_watermark_hits);
  EXPECT_EQ(suffix.size() + prefix.size(), buffer_.length());
  EXPECT_EQ(prefix + suffix, buffer_.toString());
  EXPECT_EQ(0, prefixBuffer.length());
}

TEST_F(WatermarkBufferTest, Commit) {
  buffer_.add(TEN_BYTES, 10);
  EXPECT_EQ(0, times_high_watermark_called_);
  {
    auto reservation = buffer_.reserveForRead();
    reservation.commit(10);
  }
  EXPECT_EQ(1, times_high_watermark_called_);
  EXPECT_EQ(20, buffer_.length());

  {
    auto reservation = buffer_.reserveSingleSlice(10);
    reservation.commit(10);
  }
  // Buffer is already above high watermark, so it won't be called a second time.
  EXPECT_EQ(1, times_high_watermark_called_);
  EXPECT_EQ(30, buffer_.length());
}

TEST_F(WatermarkBufferTest, Drain) {
  // Draining from above to below the low watermark does nothing if the high
  // watermark never got hit.
  buffer_.add(TEN_BYTES, 10);
  buffer_.drain(10);
  EXPECT_EQ(0, times_high_watermark_called_);
  EXPECT_EQ(0, times_low_watermark_called_);

  // Go above the high watermark then drain down to just at the low watermark.
  buffer_.add(TEN_BYTES, 11);
  buffer_.drain(5);
  EXPECT_EQ(6, buffer_.length());
  EXPECT_EQ(1, times_high_watermark_called_);
  EXPECT_EQ(0, times_low_watermark_called_);

  // Now drain below.
  buffer_.drain(1);
  EXPECT_EQ(1, times_low_watermark_called_);

  // Going back above should trigger the high again
  buffer_.add(TEN_BYTES, 10);
  EXPECT_EQ(2, times_high_watermark_called_);
}

TEST_F(WatermarkBufferTest, DrainUsingExtract) {
  // Similar to `Drain` test, but using extractMutableFrontSlice() instead of drain().
  buffer_.add(TEN_BYTES, 10);
  ASSERT_EQ(buffer_.length(), 10);
  buffer_.extractMutableFrontSlice();
  EXPECT_EQ(0, times_high_watermark_called_);
  EXPECT_EQ(0, times_low_watermark_called_);

  // Go above the high watermark then drain down to just at the low watermark.
  buffer_.appendSliceForTest(TEN_BYTES, 5);
  buffer_.appendSliceForTest(TEN_BYTES, 1);
  buffer_.appendSliceForTest(TEN_BYTES, 5);
  EXPECT_EQ(1, times_high_watermark_called_);
  EXPECT_EQ(0, times_low_watermark_called_);
  auto slice0 = buffer_.extractMutableFrontSlice(); // essentially drain(5)
  ASSERT_TRUE(slice0);
  EXPECT_EQ(slice0->getMutableData().size(), 5);
  EXPECT_EQ(6, buffer_.length());
  EXPECT_EQ(0, times_low_watermark_called_);

  // Now drain below.
  auto slice1 = buffer_.extractMutableFrontSlice(); // essentially drain(1)
  ASSERT_TRUE(slice1);
  EXPECT_EQ(slice1->getMutableData().size(), 1);
  EXPECT_EQ(1, times_high_watermark_called_);
  EXPECT_EQ(1, times_low_watermark_called_);

  // Going back above should trigger the high again.
  buffer_.add(TEN_BYTES, 10);
  EXPECT_EQ(2, times_high_watermark_called_);
}

// Verify that low watermark callback is called on drain in the case where the
// high watermark is non-zero and low watermark is 0.
TEST_F(WatermarkBufferTest, DrainWithLowWatermarkOfZero) {
  buffer_.setWatermarks(0, 10);

  // Draining from above to below the low watermark does nothing if the high
  // watermark never got hit.
  buffer_.add(TEN_BYTES, 10);
  buffer_.drain(10);
  EXPECT_EQ(0, times_high_watermark_called_);
  EXPECT_EQ(0, times_low_watermark_called_);

  // Go above the high watermark then drain down to just above the low watermark.
  buffer_.add(TEN_BYTES, 11);
  buffer_.drain(10);
  EXPECT_EQ(1, buffer_.length());
  EXPECT_EQ(0, times_low_watermark_called_);

  // Now drain below.
  buffer_.drain(1);
  EXPECT_EQ(1, times_low_watermark_called_);

  // Going back above should trigger the high again
  buffer_.add(TEN_BYTES, 11);
  EXPECT_EQ(2, times_high_watermark_called_);
}

TEST_F(WatermarkBufferTest, MoveFullBuffer) {
  buffer_.add(TEN_BYTES, 10);
  OwnedImpl data("a");

  EXPECT_EQ(0, times_high_watermark_called_);
  buffer_.move(data);
  EXPECT_EQ(1, times_high_watermark_called_);
  EXPECT_EQ(11, buffer_.length());
}

TEST_F(WatermarkBufferTest, MoveOneByte) {
  buffer_.add(TEN_BYTES, 9);
  OwnedImpl data("ab");

  buffer_.move(data, 1);
  EXPECT_EQ(0, times_high_watermark_called_);
  EXPECT_EQ(10, buffer_.length());

  buffer_.move(data, 1);
  EXPECT_EQ(1, times_high_watermark_called_);
  EXPECT_EQ(11, buffer_.length());
}

TEST_F(WatermarkBufferTest, WatermarkFdFunctions) {
  os_fd_t pipe_fds[2] = {0, 0};
#ifdef WIN32
  auto& os_sys_calls = Api::OsSysCallsSingleton::get();
  ASSERT_EQ(0, os_sys_calls.socketpair(AF_INET, SOCK_STREAM, 0, pipe_fds).rc_);
#else
  ASSERT_EQ(0, pipe(pipe_fds));
#endif

  buffer_.add(TEN_BYTES, 10);
  buffer_.add(TEN_BYTES, 10);
  EXPECT_EQ(1, times_high_watermark_called_);
  EXPECT_EQ(0, times_low_watermark_called_);

  int bytes_written_total = 0;
  Network::IoSocketHandleImpl io_handle1(pipe_fds[1]);
  while (bytes_written_total < 20) {
    Api::IoCallUint64Result result = io_handle1.write(buffer_);
    if (!result.ok()) {
      ASSERT_EQ(Api::IoError::IoErrorCode::Again, result.err_->getErrorCode());
    } else {
      bytes_written_total += result.rc_;
    }
  }
  EXPECT_EQ(1, times_high_watermark_called_);
  EXPECT_EQ(1, times_low_watermark_called_);
  EXPECT_EQ(0, buffer_.length());

  int bytes_read_total = 0;
  Network::IoSocketHandleImpl io_handle2(pipe_fds[0]);
  while (bytes_read_total < 20) {
    Api::IoCallUint64Result result = io_handle2.read(buffer_, 20);
    bytes_read_total += result.rc_;
  }
  EXPECT_EQ(2, times_high_watermark_called_);
  EXPECT_EQ(20, buffer_.length());
}

TEST_F(WatermarkBufferTest, MoveWatermarks) {
  buffer_.add(TEN_BYTES, 9);
  EXPECT_EQ(0, times_high_watermark_called_);
  buffer_.setWatermarks(1, 9);
  EXPECT_EQ(0, times_high_watermark_called_);
  buffer_.setWatermarks(1, 8);
  EXPECT_EQ(1, times_high_watermark_called_);

  buffer_.setWatermarks(8, 20);
  EXPECT_EQ(0, times_low_watermark_called_);
  buffer_.setWatermarks(9, 20);
  EXPECT_EQ(1, times_low_watermark_called_);
  buffer_.setWatermarks(7, 20);
  EXPECT_EQ(1, times_low_watermark_called_);
  buffer_.setWatermarks(9, 20);
  EXPECT_EQ(1, times_low_watermark_called_);
  EXPECT_EQ(0, times_overflow_watermark_called_);

  EXPECT_EQ(1, times_high_watermark_called_);
  buffer_.setWatermarks(2);
  EXPECT_EQ(2, times_high_watermark_called_);
  EXPECT_EQ(1, times_low_watermark_called_);
  EXPECT_EQ(0, times_overflow_watermark_called_);
  buffer_.setWatermarks(0);
  EXPECT_EQ(2, times_high_watermark_called_);
  EXPECT_EQ(2, times_low_watermark_called_);
  EXPECT_EQ(0, times_overflow_watermark_called_);
  buffer_.setWatermarks(1);
  EXPECT_EQ(3, times_high_watermark_called_);
  EXPECT_EQ(2, times_low_watermark_called_);
  EXPECT_EQ(0, times_overflow_watermark_called_);

  // Fully drain the buffer.
  buffer_.drain(9);
  EXPECT_EQ(3, times_low_watermark_called_);
  EXPECT_EQ(0, buffer_.length());
  EXPECT_EQ(0, times_overflow_watermark_called_);
}

TEST_F(WatermarkBufferTest, GetRawSlices) {
  buffer_.add(TEN_BYTES, 10);

  RawSliceVector slices = buffer_.getRawSlices(/*max_slices=*/2);
  ASSERT_EQ(1, slices.size());
  EXPECT_EQ(10, slices[0].len_);
  EXPECT_EQ(0, memcmp(slices[0].mem_, &TEN_BYTES[0], 10));

  void* data_pointer = buffer_.linearize(10);
  EXPECT_EQ(data_pointer, slices[0].mem_);
}

TEST_F(WatermarkBufferTest, Search) {
  buffer_.add(TEN_BYTES, 10);

  EXPECT_EQ(1, buffer_.search(&TEN_BYTES[1], 2, 0, 0));

  EXPECT_EQ(-1, buffer_.search(&TEN_BYTES[1], 2, 5, 0));
}

TEST_F(WatermarkBufferTest, StartsWith) {
  buffer_.add(TEN_BYTES, 10);

  EXPECT_TRUE(buffer_.startsWith({TEN_BYTES, 2}));
  EXPECT_TRUE(buffer_.startsWith({TEN_BYTES, 10}));
  EXPECT_FALSE(buffer_.startsWith({&TEN_BYTES[1], 2}));
}

TEST_F(WatermarkBufferTest, MoveBackWithWatermarks) {
  int high_watermark_buffer1 = 0;
  int low_watermark_buffer1 = 0;
  int overflow_watermark_buffer1 = 0;
  Buffer::WatermarkBuffer buffer1{[&]() -> void { ++low_watermark_buffer1; },
                                  [&]() -> void { ++high_watermark_buffer1; },
                                  [&]() -> void { ++overflow_watermark_buffer1; }};
  buffer1.setWatermarks(5, 10);

  // Stick 20 bytes in buffer_ and expect the high watermark is hit.
  buffer_.add(TEN_BYTES, 10);
  buffer_.add(TEN_BYTES, 10);
  EXPECT_EQ(1, times_high_watermark_called_);

  // Now move 10 bytes to the new buffer. Nothing should happen.
  buffer1.move(buffer_, 10);
  EXPECT_EQ(0, times_low_watermark_called_);
  EXPECT_EQ(0, high_watermark_buffer1);
  EXPECT_EQ(0, overflow_watermark_buffer1);

  // Move 10 more bytes to the new buffer. Both buffers should hit watermark callbacks.
  buffer1.move(buffer_, 10);
  EXPECT_EQ(1, times_low_watermark_called_);
  EXPECT_EQ(1, high_watermark_buffer1);
  EXPECT_EQ(0, times_overflow_watermark_called_);
  EXPECT_EQ(0, overflow_watermark_buffer1);

  // Now move all the data back to the original buffer. Watermarks should trigger immediately.
  buffer_.move(buffer1);
  EXPECT_EQ(2, times_high_watermark_called_);
  EXPECT_EQ(1, low_watermark_buffer1);
  EXPECT_EQ(0, times_overflow_watermark_called_);
  EXPECT_EQ(0, overflow_watermark_buffer1);
}

TEST_F(WatermarkBufferTest, OverflowWatermark) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues({{"envoy.buffer.overflow_multiplier", "2"}});

  int high_watermark_buffer1 = 0;
  int low_watermark_buffer1 = 0;
  int overflow_watermark_buffer1 = 0;
  Buffer::WatermarkBuffer buffer1{[&]() -> void { ++low_watermark_buffer1; },
                                  [&]() -> void { ++high_watermark_buffer1; },
                                  [&]() -> void { ++overflow_watermark_buffer1; }};
  buffer1.setWatermarks(5, 10);

  buffer1.add(TEN_BYTES, 10);
  EXPECT_EQ(0, high_watermark_buffer1);
  EXPECT_EQ(0, overflow_watermark_buffer1);
  buffer1.add("a", 1);
  EXPECT_EQ(1, high_watermark_buffer1);
  EXPECT_EQ(0, overflow_watermark_buffer1);
  buffer1.add(TEN_BYTES, 9);
  EXPECT_EQ(1, high_watermark_buffer1);
  EXPECT_EQ(0, overflow_watermark_buffer1);
  buffer1.add("a", 1);
  EXPECT_EQ(1, high_watermark_buffer1);
  EXPECT_EQ(1, overflow_watermark_buffer1);
  EXPECT_EQ(21, buffer1.length());
  buffer1.add("a", 1);
  EXPECT_EQ(1, high_watermark_buffer1);
  EXPECT_EQ(1, overflow_watermark_buffer1);
  EXPECT_EQ(22, buffer1.length());

  // Overflow is only triggered once
  buffer1.drain(18);
  EXPECT_EQ(4, buffer1.length());
  EXPECT_EQ(1, high_watermark_buffer1);
  EXPECT_EQ(1, low_watermark_buffer1);
  EXPECT_EQ(1, overflow_watermark_buffer1);
  buffer1.add(TEN_BYTES, 10);
  EXPECT_EQ(2, high_watermark_buffer1);
  EXPECT_EQ(1, overflow_watermark_buffer1);
  EXPECT_EQ(14, buffer1.length());
  buffer1.add(TEN_BYTES, 6);
  EXPECT_EQ(2, high_watermark_buffer1);
  EXPECT_EQ(1, overflow_watermark_buffer1);
  EXPECT_EQ(20, buffer1.length());
}

TEST_F(WatermarkBufferTest, OverflowWatermarkDisabled) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues({{"envoy.buffer.overflow_multiplier", "0"}});

  int high_watermark_buffer1 = 0;
  int low_watermark_buffer1 = 0;
  int overflow_watermark_buffer1 = 0;
  Buffer::WatermarkBuffer buffer1{[&]() -> void { ++low_watermark_buffer1; },
                                  [&]() -> void { ++high_watermark_buffer1; },
                                  [&]() -> void { ++overflow_watermark_buffer1; }};
  buffer1.setWatermarks(5, 10);

  buffer1.add(TEN_BYTES, 10);
  EXPECT_EQ(0, high_watermark_buffer1);
  EXPECT_EQ(0, overflow_watermark_buffer1);
  buffer1.add("a", 1);
  EXPECT_EQ(1, high_watermark_buffer1);
  EXPECT_EQ(0, overflow_watermark_buffer1);
  buffer1.add(TEN_BYTES, 10);
  EXPECT_EQ(1, high_watermark_buffer1);
  EXPECT_EQ(0, overflow_watermark_buffer1);
  EXPECT_EQ(21, buffer1.length());
}

TEST_F(WatermarkBufferTest, OverflowWatermarkDisabledOnVeryHighValue) {
// Disabling execution with TSAN as it causes the test to use too much memory
// and time, making the test fail in some settings (such as CI)
#if defined(__has_feature) && __has_feature(thread_sanitizer)
  ENVOY_LOG_MISC(critical, "WatermarkBufferTest::OverflowWatermarkDisabledOnVeryHighValue not "
                           "supported by this compiler configuration");
#else
  // Verifies that the overflow watermark is disabled when its value is higher
  // than uint32_t max value
  TestScopedRuntime scoped_runtime;

  int high_watermark_buffer1 = 0;
  int overflow_watermark_buffer1 = 0;
  Buffer::WatermarkBuffer buffer1{[&]() -> void {}, [&]() -> void { ++high_watermark_buffer1; },
                                  [&]() -> void { ++overflow_watermark_buffer1; }};

  // Make sure the overflow threshold will be above std::numeric_limits<uint32_t>::max()
  const uint64_t overflow_multiplier = 3;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.buffer.overflow_multiplier", std::to_string(overflow_multiplier)}});
  const uint32_t high_watermark_threshold =
      (std::numeric_limits<uint32_t>::max() / overflow_multiplier) + 1;
  buffer1.setWatermarks(high_watermark_threshold);

  // Add many segments instead of full uint32_t::max to get around std::bad_alloc exception
  const uint32_t segment_denominator = 128;
  const uint32_t big_segment_len = std::numeric_limits<uint32_t>::max() / segment_denominator + 1;
  for (uint32_t i = 0; i < segment_denominator; ++i) {
    auto reservation = buffer1.reserveSingleSlice(big_segment_len);
    reservation.commit(big_segment_len);
  }
  EXPECT_GT(buffer1.length(), std::numeric_limits<uint32_t>::max());
  EXPECT_LT(buffer1.length(), high_watermark_threshold * overflow_multiplier);
  EXPECT_EQ(1, high_watermark_buffer1);
  EXPECT_EQ(0, overflow_watermark_buffer1);

  // Reserve and commit additional space on the buffer beyond the expected
  // high_watermark_threshold * overflow_multiplier threshold.
  const uint64_t size = high_watermark_threshold * overflow_multiplier - buffer1.length() + 1;
  auto reservation = buffer1.reserveSingleSlice(size);
  reservation.commit(size);
  EXPECT_EQ(buffer1.length(), high_watermark_threshold * overflow_multiplier + 1);
  EXPECT_EQ(1, high_watermark_buffer1);
  EXPECT_EQ(0, overflow_watermark_buffer1);
#endif
}

TEST_F(WatermarkBufferTest, OverflowWatermarkEqualHighWatermark) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues({{"envoy.buffer.overflow_multiplier", "1"}});

  int high_watermark_buffer1 = 0;
  int low_watermark_buffer1 = 0;
  int overflow_watermark_buffer1 = 0;
  Buffer::WatermarkBuffer buffer1{[&]() -> void { ++low_watermark_buffer1; },
                                  [&]() -> void { ++high_watermark_buffer1; },
                                  [&]() -> void { ++overflow_watermark_buffer1; }};
  buffer1.setWatermarks(5, 10);

  buffer1.add(TEN_BYTES, 10);
  EXPECT_EQ(0, high_watermark_buffer1);
  EXPECT_EQ(0, overflow_watermark_buffer1);
  buffer1.add("a", 1);
  EXPECT_EQ(0, low_watermark_buffer1);
  EXPECT_EQ(1, high_watermark_buffer1);
  EXPECT_EQ(1, overflow_watermark_buffer1);

  buffer1.drain(6);
  EXPECT_EQ(1, low_watermark_buffer1);
  EXPECT_EQ(1, high_watermark_buffer1);
  EXPECT_EQ(1, overflow_watermark_buffer1);
  buffer1.add(TEN_BYTES, 10);
  EXPECT_EQ(15, buffer1.length());
  EXPECT_EQ(2, high_watermark_buffer1);
  EXPECT_EQ(1, overflow_watermark_buffer1);
}

TEST_F(WatermarkBufferTest, MoveWatermarksOverflow) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues({{"envoy.buffer.overflow_multiplier", "2"}});

  int high_watermark_buffer1 = 0;
  int low_watermark_buffer1 = 0;
  int overflow_watermark_buffer1 = 0;
  Buffer::WatermarkBuffer buffer1{[&]() -> void { ++low_watermark_buffer1; },
                                  [&]() -> void { ++high_watermark_buffer1; },
                                  [&]() -> void { ++overflow_watermark_buffer1; }};
  buffer1.setWatermarks(5, 10);
  buffer1.add(TEN_BYTES, 9);
  EXPECT_EQ(0, high_watermark_buffer1);
  EXPECT_EQ(0, overflow_watermark_buffer1);
  buffer1.setWatermarks(1, 9);
  EXPECT_EQ(0, high_watermark_buffer1);
  EXPECT_EQ(0, overflow_watermark_buffer1);
  buffer1.setWatermarks(1, 8);
  EXPECT_EQ(1, high_watermark_buffer1);
  EXPECT_EQ(0, overflow_watermark_buffer1);
  buffer1.setWatermarks(1, 5);
  EXPECT_EQ(1, high_watermark_buffer1);
  EXPECT_EQ(0, overflow_watermark_buffer1);
  buffer1.setWatermarks(1, 4);
  EXPECT_EQ(1, high_watermark_buffer1);
  EXPECT_EQ(1, overflow_watermark_buffer1);

  // Overflow is only triggered once
  buffer1.setWatermarks(3, 6);
  EXPECT_EQ(0, low_watermark_buffer1);
  EXPECT_EQ(1, high_watermark_buffer1);
  EXPECT_EQ(1, overflow_watermark_buffer1);
  buffer1.drain(7);
  buffer1.add(TEN_BYTES, 9);
  EXPECT_EQ(11, buffer1.length());
  EXPECT_EQ(1, low_watermark_buffer1);
  EXPECT_EQ(2, high_watermark_buffer1);
  EXPECT_EQ(1, overflow_watermark_buffer1);
}

} // namespace
} // namespace Buffer
} // namespace Envoy
