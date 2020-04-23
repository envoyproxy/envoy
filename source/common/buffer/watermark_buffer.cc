#include "common/buffer/watermark_buffer.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Buffer {

void WatermarkBuffer::add(const void* data, uint64_t size) {
  OwnedImpl::add(data, size);
  checkOverflowAndHighWatermarks();
}

void WatermarkBuffer::add(absl::string_view data) {
  OwnedImpl::add(data);
  checkOverflowAndHighWatermarks();
}

void WatermarkBuffer::add(const Instance& data) {
  OwnedImpl::add(data);
  checkOverflowAndHighWatermarks();
}

void WatermarkBuffer::prepend(absl::string_view data) {
  OwnedImpl::prepend(data);
  checkOverflowAndHighWatermarks();
}

void WatermarkBuffer::prepend(Instance& data) {
  OwnedImpl::prepend(data);
  checkOverflowAndHighWatermarks();
}

void WatermarkBuffer::commit(RawSlice* iovecs, uint64_t num_iovecs) {
  OwnedImpl::commit(iovecs, num_iovecs);
  checkOverflowAndHighWatermarks();
}

void WatermarkBuffer::drain(uint64_t size) {
  OwnedImpl::drain(size);
  checkLowWatermark();
}

void WatermarkBuffer::move(Instance& rhs) {
  OwnedImpl::move(rhs);
  checkOverflowAndHighWatermarks();
}

void WatermarkBuffer::move(Instance& rhs, uint64_t length) {
  OwnedImpl::move(rhs, length);
  checkOverflowAndHighWatermarks();
}

Api::IoCallUint64Result WatermarkBuffer::read(Network::IoHandle& io_handle, uint64_t max_length) {
  Api::IoCallUint64Result result = OwnedImpl::read(io_handle, max_length);
  checkOverflowAndHighWatermarks();
  return result;
}

uint64_t WatermarkBuffer::reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) {
  uint64_t bytes_reserved = OwnedImpl::reserve(length, iovecs, num_iovecs);
  checkOverflowAndHighWatermarks();
  return bytes_reserved;
}

Api::IoCallUint64Result WatermarkBuffer::write(Network::IoHandle& io_handle) {
  Api::IoCallUint64Result result = OwnedImpl::write(io_handle);
  checkLowWatermark();
  return result;
}

void WatermarkBuffer::setWatermarks(uint32_t low_watermark, uint32_t high_watermark, uint32_t overflow_watermark) {
  ASSERT((low_watermark < high_watermark || (high_watermark == 0 && low_watermark == 0)) &&
         (overflow_watermark == 0 || overflow_watermark > high_watermark));
  low_watermark_ = low_watermark;
  high_watermark_ = high_watermark;
  overflow_watermark_ = overflow_watermark;
  checkOverflowAndHighWatermarks();
  checkLowWatermark();
}

void WatermarkBuffer::checkLowWatermark() {
  if (!above_high_watermark_called_ ||
      (high_watermark_ != 0 && OwnedImpl::length() > low_watermark_)) {
    return;
  }

  above_high_watermark_called_ = false;
  below_low_watermark_();
}

void WatermarkBuffer::checkOverflowAndHighWatermarks() {
  // Check if overflow watermark is enabled, wasn't previously triggered,
  // and the buffer size is above the threshold
  if (above_overflow_watermark_ && overflow_watermark_ != 0 &&
      !above_overflow_watermark_called_ && OwnedImpl::length() > overflow_watermark_) {
    above_overflow_watermark_called_ = true;
    above_overflow_watermark_();
  }

  if (above_high_watermark_called_ || high_watermark_ == 0 ||
      OwnedImpl::length() <= high_watermark_) {
    return;
  }

  above_high_watermark_called_ = true;
  above_high_watermark_();
}

} // namespace Buffer
} // namespace Envoy
