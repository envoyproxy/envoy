#pragma once

#include <functional>

#include "source/common/common/assert.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Quic {

// A class, together with a stand alone buffer, used to achieve the purpose of WatermarkBuffer.
// Itself doesn't have buffer or bookkeep buffered bytes. But provided with buffered_bytes,
// it reacts upon crossing high/low watermarks.
// It's no-op if provided low and high watermark are 0.
class EnvoyQuicSimulatedWatermarkBuffer {
public:
  EnvoyQuicSimulatedWatermarkBuffer(uint32_t low_watermark, uint32_t high_watermark,
                                    std::function<void()> below_low_watermark,
                                    std::function<void()> above_high_watermark,
                                    spdlog::logger& logger)
      : low_watermark_(low_watermark), high_watermark_(high_watermark),
        below_low_watermark_(std::move(below_low_watermark)),
        above_high_watermark_(std::move(above_high_watermark)), logger_(logger) {
    ASSERT((high_watermark == 0 && low_watermark == 0) || (high_watermark_ > low_watermark_));
  }

  uint32_t highWatermark() const { return high_watermark_; }

  uint32_t lowWatermark() const { return low_watermark_; }

  void checkHighWatermark(uint32_t bytes_buffered) {
    if (high_watermark_ > 0 && !is_full_ && bytes_buffered > high_watermark_) {
      // Transitioning from below low watermark to above high watermark.
      ENVOY_LOG_TO_LOGGER(logger_, debug, "Buffered {} bytes, cross high watermark {}",
                          bytes_buffered, high_watermark_);
      is_full_ = true;
      above_high_watermark_();
    }
  }

  void checkLowWatermark(uint32_t bytes_buffered) {
    if (low_watermark_ > 0 && is_full_ && bytes_buffered < low_watermark_) {
      // Transitioning from above high watermark to below low watermark.
      ENVOY_LOG_TO_LOGGER(logger_, debug, "Buffered {} bytes, cross low watermark {}",
                          bytes_buffered, low_watermark_);
      is_full_ = false;
      below_low_watermark_();
    }
  }

  // True after the buffer goes above high watermark and hasn't come down below low
  // watermark yet, even though the buffered data might be between high and low
  // watermarks.
  bool isAboveHighWatermark() const { return is_full_; }

  // True before the buffer crosses the high watermark for the first time and after the buffer goes
  // below low watermark and hasn't come up above high watermark yet, even though the buffered data
  // might be between high and low watermarks.
  bool isBelowLowWatermark() const { return !is_full_; }

private:
  uint32_t low_watermark_{0};
  uint32_t high_watermark_{0};
  bool is_full_{false};
  std::function<void()> below_low_watermark_;
  std::function<void()> above_high_watermark_;
  spdlog::logger& logger_;
};

} // namespace Quic
} // namespace Envoy
