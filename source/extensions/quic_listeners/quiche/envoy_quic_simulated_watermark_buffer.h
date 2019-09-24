#pragma once

#include <functional>

namespace Envoy {
namespace Quic {

// A class, together with a stand alone buffer, used to achieve the purpose of WatermarkBuffer.
// Itself doesn't have buffer or do bookeeping of buffered bytes. But provided buffered_bytes, it
// re-acts upon crossing high/low watermarks.
class EnvoyQuicSimulatedWatermarkBuffer {
public:
  EnvoyQuicSimulatedWatermarkBuffer(uint32_t low_watermark, uint32_t high_watermark,
                                    std::function<void()> below_low_watermark,
                                    std::function<void()> above_high_watermark)
      : low_watermark_(low_watermark), high_watermark_(high_watermark),
        below_low_watermark_(std::move(below_low_watermark)),
        above_high_watermark_(std::move(above_high_watermark)) {
    ASSERT(high_watermark == 0 && low_watermark == 0 || high_watermark_ > low_watermark_);
  }

  void checkHighWatermark(uint32_t bytes_buffered) {
    if (high_watermark_ > 0 && !is_above_high_watermark_ && bytes_buffered > high_watermark_) {
      // Just exceeds high watermark.
      is_above_high_watermark_ = true;
      is_below_low_watermark_ = false;
      above_high_watermark_();
    }
  }

  void checkLowWatermark(uint32_t bytes_buffered) {
    if (low_watermark_ > 0 && !is_below_low_watermark_ && bytes_buffered < low_watermark_) {
      // Just cross low watermark.
      is_below_low_watermark_ = true;
      is_above_high_watermark_ = false;
      below_low_watermark_();
    }
  }

  bool isAboveHighWatermark() const { return is_above_high_watermark_; }

  bool isBelowLowWatermark() const { return is_below_low_watermark_; }

private:
  uint32_t low_watermark_{0};
  bool is_below_low_watermark_{true};
  uint32_t high_watermark_{0};
  bool is_above_high_watermark_{false};
  std::function<void()> below_low_watermark_;
  std::function<void()> above_high_watermark_;
};

} // namespace Quic
} // namespace Envoy
