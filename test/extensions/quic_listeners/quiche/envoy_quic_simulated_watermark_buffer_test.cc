#include "extensions/quic_listeners/quiche/envoy_quic_simulated_watermark_buffer.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicSimulatedWatermarkBufferTest : public ::testing::Test,
                                              protected Logger::Loggable<Logger::Id::testing> {
public:
  EnvoyQuicSimulatedWatermarkBufferTest()
      : simulated_watermark_buffer_(
            low_watermark_, high_watermark_, [this]() { onBelowLowWatermark(); },
            [this]() { onAboveHighWatermark(); }, ENVOY_LOGGER()) {}

  void onAboveHighWatermark() { ++above_high_watermark_; }

  void onBelowLowWatermark() { ++below_low_watermark_; }

protected:
  size_t above_high_watermark_{0};
  size_t below_low_watermark_{0};
  uint32_t high_watermark_{100};
  uint32_t low_watermark_{60};
  EnvoyQuicSimulatedWatermarkBuffer simulated_watermark_buffer_;
};

TEST_F(EnvoyQuicSimulatedWatermarkBufferTest, InitialState) {
  EXPECT_TRUE(simulated_watermark_buffer_.isBelowLowWatermark());
  EXPECT_FALSE(simulated_watermark_buffer_.isAboveHighWatermark());
  EXPECT_EQ(high_watermark_, simulated_watermark_buffer_.highWatermark());
}

TEST_F(EnvoyQuicSimulatedWatermarkBufferTest, GoAboveHighWatermarkAndComeDown) {
  simulated_watermark_buffer_.checkHighWatermark(low_watermark_ + 1);
  EXPECT_EQ(0U, above_high_watermark_);
  // Even though the buffered data is above low watermark, the buffer is still regarded
  // as below watermark because it didn't reach high watermark.
  EXPECT_TRUE(simulated_watermark_buffer_.isBelowLowWatermark());
  simulated_watermark_buffer_.checkLowWatermark(low_watermark_ - 1);
  // Going down below low watermark shouldn't trigger callback as it never
  // reached high watermark.
  EXPECT_EQ(0U, below_low_watermark_);

  simulated_watermark_buffer_.checkHighWatermark(high_watermark_ + 1);
  EXPECT_EQ(1U, above_high_watermark_);
  EXPECT_TRUE(simulated_watermark_buffer_.isAboveHighWatermark());
  EXPECT_FALSE(simulated_watermark_buffer_.isBelowLowWatermark());

  simulated_watermark_buffer_.checkHighWatermark(high_watermark_ + 10);
  EXPECT_EQ(1U, above_high_watermark_);

  simulated_watermark_buffer_.checkLowWatermark(low_watermark_);
  EXPECT_EQ(0U, below_low_watermark_);

  simulated_watermark_buffer_.checkHighWatermark(high_watermark_ + 10);
  // Crossing high watermark continuously shouldn't trigger callback.
  EXPECT_EQ(1U, above_high_watermark_);

  // Crossing low watermark after coming down from high watermark should trigger
  // callback and change status.
  simulated_watermark_buffer_.checkLowWatermark(low_watermark_ - 1);
  EXPECT_EQ(1U, below_low_watermark_);
  EXPECT_TRUE(simulated_watermark_buffer_.isBelowLowWatermark());
  EXPECT_FALSE(simulated_watermark_buffer_.isAboveHighWatermark());
}

TEST_F(EnvoyQuicSimulatedWatermarkBufferTest, NoWatermarkSpecified) {
  EnvoyQuicSimulatedWatermarkBuffer buffer(
      0, 0, [this]() { onBelowLowWatermark(); }, [this]() { onAboveHighWatermark(); },
      ENVOY_LOGGER());
  buffer.checkHighWatermark(10);
  EXPECT_EQ(0U, above_high_watermark_);

  simulated_watermark_buffer_.checkLowWatermark(0);
  EXPECT_EQ(0U, below_low_watermark_);
  EXPECT_TRUE(simulated_watermark_buffer_.isBelowLowWatermark());
}

} // namespace Quic
} // namespace Envoy
