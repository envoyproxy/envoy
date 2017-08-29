#pragma once

#include <functional>
#include <string>

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Buffer {

// A subclass of OwnedImpl which does watermark validation.
// Each time the buffer is resized (written to or drained), the watermarks are checked.  As the
// buffer size transitions from under the low watermark to above the high watermark, the
// above_high_watermark function is called one time. It will not be called again until the buffer
// is drained below the low watermark, at which point the below_low_watermark function is called.
class WatermarkBuffer : public OwnedImpl, public virtual WatermarkInstance {
public:
  WatermarkBuffer(std::function<void()> below_low_watermark,
                  std::function<void()> above_high_watermark)
      : below_low_watermark_(below_low_watermark), above_high_watermark_(above_high_watermark) {}

  // Override all functions from Instance which can result in changing the size
  // of the underlying buffer.
  void add(const void* data, uint64_t size) override;
  void add(const std::string& data) override;
  void add(const Instance& data) override;
  void commit(RawSlice* iovecs, uint64_t num_iovecs) override;
  void drain(uint64_t size) override;
  void move(Instance& rhs) override;
  void move(Instance& rhs, uint64_t length) override;
  int read(int fd, uint64_t max_length) override;
  uint64_t reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) override;
  int write(int fd) override;
  void postProcess() override { checkLowWatermark(); }

  void setWatermarks(uint32_t watermark) override {
    if (watermark != 0) {
      setWatermarks(watermark / 2, watermark);
    }
  }
  void setWatermarks(uint32_t low_watermark, uint32_t high_watermark) override;

private:
  void checkHighWatermark();
  void checkLowWatermark();

  std::function<void()> below_low_watermark_;
  std::function<void()> above_high_watermark_;

  // Used for enforcing buffer limits (off by default).  If these are set to non-zero by a call to
  // setWatermarks() the watermark callbacks will be called as described above.
  uint32_t high_watermark_{0};
  uint32_t low_watermark_{0};
  // Tracks the latest state of watermark callbacks.
  // True between the time above_high_watermark_ has been called until above_high_watermark_ has
  // been called.
  bool above_high_watermark_called_{false};
};

class WatermarkBufferFactory : public WatermarkInstanceFactory {
public:
  // Buffer::WatermarkInstanceFactory
  WatermarkInstancePtr create(std::function<void()> below_low_watermark,
                              std::function<void()> above_high_watermark) override {
    return WatermarkInstancePtr{new WatermarkBuffer(below_low_watermark, above_high_watermark)};
  }
};

} // namespace Buffer
} // namespace Envoy
