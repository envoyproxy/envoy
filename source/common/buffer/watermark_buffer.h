#pragma once

#include <functional>
#include <string>

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Buffer {

// A wrapper for an underlying buffer which does watermark validation.
// The underlying buffer's ownership is transfered to the Watermark buffer.   Each time the inner
// buffer is resized (written to or drained), the watermarks are checked.  As the buffer size
// transitions from under the low watermark to above the high watermark, the above_high_watermark
// function is called one time. It will not be called again until the buffer is drained below the
// low watermark, at which point the below_low_watermark function is called.
//
// Because the internals of OwnedImpl::move() require accessing the underlying data, OwnedImpl is
// not compatible with generic Buffer::Impls.  To allow compatability between WatermarkBuffer and
// OwnedImpl::move, WatermarkBuffer must implement LibEventInstance and is also not compatible
// with generic Buffer::Impls.
//
// WatermarkBuffer takes a pointer to a generic InstancePtr in the constructor to allow test mocks
// which overrides move() in any case.
class WatermarkBuffer : public LibEventInstance {
public:
  WatermarkBuffer(InstancePtr&& buffer, std::function<void()> below_low_watermark,
                  std::function<void()> above_high_watermark)
      : wrapped_buffer_(std::move(buffer)), below_low_watermark_(below_low_watermark),
        above_high_watermark_(above_high_watermark) {}

  // Instance
  void add(const void* data, uint64_t size) override;
  void add(const std::string& data) override;
  void add(const Instance& data) override;
  void commit(RawSlice* iovecs, uint64_t num_iovecs) override;
  void drain(uint64_t size) override;
  uint64_t getRawSlices(RawSlice* out, uint64_t out_size) const override {
    return wrapped_buffer_->getRawSlices(out, out_size);
  }
  uint64_t length() const override { return wrapped_buffer_->length(); }
  void* linearize(uint32_t size) override { return wrapped_buffer_->linearize(size); }
  void move(Instance& rhs) override;
  void move(Instance& rhs, uint64_t length) override;
  int read(int fd, uint64_t max_length) override;
  uint64_t reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) override;
  ssize_t search(const void* data, uint64_t size, size_t start) const override {
    return wrapped_buffer_->search(data, size, start);
  }
  int write(int fd) override;
  Event::Libevent::BufferPtr& buffer() override {
    return static_cast<LibEventInstance&>(*wrapped_buffer_).buffer();
  }
  void postProcess() override { checkLowWatermark(); }

  void setWatermarks(uint32_t low_watermark, uint32_t high_watermark);

private:
  void checkHighWatermark();
  void checkLowWatermark();

  InstancePtr wrapped_buffer_;
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

} // namespace Buffer
} // namespace Envoy
