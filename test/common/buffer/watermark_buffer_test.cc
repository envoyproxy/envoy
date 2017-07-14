#include "common/buffer/buffer_impl.h"
#include "common/buffer/watermark_buffer.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Buffer {
namespace {

const char TEN_BYTES[] = "0123456789";

class WatermarkBufferTest : public testing::Test {
public:
  WatermarkBufferTest() { buffer_.setWatermarks(5, 10); }

  Buffer::WatermarkBuffer buffer_{InstancePtr{new OwnedImpl()},
                                  [&]() -> void { ++times_low_watermark_called_; },
                                  [&]() -> void { ++times_high_watermark_called_; }};
  uint32_t times_low_watermark_called_{0};
  uint32_t times_high_watermark_called_{0};
};

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

TEST_F(WatermarkBufferTest, Commit) {
  buffer_.add(TEN_BYTES, 10);
  EXPECT_EQ(0, times_high_watermark_called_);
  RawSlice out;
  buffer_.reserve(10, &out, 1);
  memcpy(out.mem_, &TEN_BYTES[0], 10);
  out.len_ = 10;
  buffer_.commit(&out, 1);
  EXPECT_EQ(1, times_high_watermark_called_);
  EXPECT_EQ(20, buffer_.length());
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
  buffer_.drain(6);
  EXPECT_EQ(5, buffer_.length());
  EXPECT_EQ(0, times_low_watermark_called_);

  // Now drain below.
  buffer_.drain(1);
  EXPECT_EQ(1, times_low_watermark_called_);

  // Going back above should trigger the high again
  buffer_.add(TEN_BYTES, 10);
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

TEST_F(WatermarkBufferTest, MoveWatermarks) {
  buffer_.add(TEN_BYTES, 9);
  EXPECT_EQ(0, times_high_watermark_called_);
  buffer_.setWatermarks(1, 9);
  EXPECT_EQ(0, times_high_watermark_called_);
  buffer_.setWatermarks(1, 8);
  EXPECT_EQ(1, times_high_watermark_called_);

  buffer_.setWatermarks(9, 20);
  EXPECT_EQ(0, times_low_watermark_called_);
  buffer_.setWatermarks(10, 20);
  EXPECT_EQ(1, times_low_watermark_called_);
  buffer_.setWatermarks(8, 20);
  buffer_.setWatermarks(10, 20);
  EXPECT_EQ(1, times_low_watermark_called_);
}

} // namespace
} // namespace Buffer
} // namespace Envoy
