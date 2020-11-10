#pragma once

#include <functional>
#include <string>

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Buffer {

// A subclass of OwnedImpl which does watermark validation.
// Each time the buffer is resized (written to or drained), the watermarks are checked. As the
// buffer size transitions from under the low watermark to above the high watermark, the
// above_high_watermark function is called one time. It will not be called again until the buffer
// is drained below the low watermark, at which point the below_low_watermark function is called.
// If the buffer size is above the overflow watermark, above_overflow_watermark is called.
// It is only called on the first time the buffer overflows.
class WatermarkBuffer : public OwnedImpl {
public:
  WatermarkBuffer(std::function<void()> below_low_watermark,
                  std::function<void()> above_high_watermark,
                  std::function<void()> above_overflow_watermark)
      : below_low_watermark_(below_low_watermark), above_high_watermark_(above_high_watermark),
        above_overflow_watermark_(above_overflow_watermark) {}

  // Override all functions from Instance which can result in changing the size
  // of the underlying buffer.
  void add(const void* data, uint64_t size) override;
  void add(absl::string_view data) override;
  void add(const Instance& data) override;
  void prepend(absl::string_view data) override;
  void prepend(Instance& data) override;
  void commit(RawSlice* iovecs, uint64_t num_iovecs) override;
  void drain(uint64_t size) override;
  void move(Instance& rhs) override;
  void move(Instance& rhs, uint64_t length) override;
  SliceDataPtr extractMutableFrontSlice() override;
  uint64_t reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) override;
  void postProcess() override { checkLowWatermark(); }
  void appendSliceForTest(const void* data, uint64_t size) override;
  void appendSliceForTest(absl::string_view data) override;

  void setWatermarks(uint32_t watermark) { setWatermarks(watermark / 2, watermark); }
  void setWatermarks(uint32_t low_watermark, uint32_t high_watermark);
  uint32_t highWatermark() const { return high_watermark_; }
  // Returns true if the high watermark callbacks have been called more recently
  // than the low watermark callbacks.
  bool highWatermarkTriggered() const { return above_high_watermark_called_; }

private:
  void checkHighAndOverflowWatermarks();
  void checkLowWatermark();

  std::function<void()> below_low_watermark_;
  std::function<void()> above_high_watermark_;
  std::function<void()> above_overflow_watermark_;

  // Used for enforcing buffer limits (off by default). If these are set to non-zero by a call to
  // setWatermarks() the watermark callbacks will be called as described above.
  uint32_t high_watermark_{0};
  uint32_t low_watermark_{0};
  uint32_t overflow_watermark_{0};
  // Tracks the latest state of watermark callbacks.
  // True between the time above_high_watermark_ has been called until above_high_watermark_ has
  // been called.
  bool above_high_watermark_called_{false};
  // Set to true when above_overflow_watermark_ is called (and isn't cleared).
  bool above_overflow_watermark_called_{false};
};

using WatermarkBufferPtr = std::unique_ptr<WatermarkBuffer>;

class WatermarkBufferFactory : public WatermarkFactory {
public:
  // Buffer::WatermarkFactory
  InstancePtr create(std::function<void()> below_low_watermark,
                     std::function<void()> above_high_watermark,
                     std::function<void()> above_overflow_watermark) override {
    return std::make_unique<WatermarkBuffer>(below_low_watermark, above_high_watermark,
                                             above_overflow_watermark);
  }
};

} // namespace Buffer
} // namespace Envoy
