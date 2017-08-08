#include "common/buffer/watermark_buffer.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Buffer {

void WatermarkBuffer::add(const void* data, uint64_t size) {
  wrapped_buffer_->add(data, size);
  checkHighWatermark();
}

void WatermarkBuffer::add(const std::string& data) {
  wrapped_buffer_->add(data);
  checkHighWatermark();
}

void WatermarkBuffer::add(const Instance& data) {
  wrapped_buffer_->add(data);
  checkHighWatermark();
}

void WatermarkBuffer::commit(RawSlice* iovecs, uint64_t num_iovecs) {
  wrapped_buffer_->commit(iovecs, num_iovecs);
  checkHighWatermark();
}

void WatermarkBuffer::drain(uint64_t size) {
  wrapped_buffer_->drain(size);
  checkLowWatermark();
}

void WatermarkBuffer::move(Instance& rhs) {
  wrapped_buffer_->move(rhs);
  checkHighWatermark();
}

void WatermarkBuffer::move(Instance& rhs, uint64_t length) {
  wrapped_buffer_->move(rhs, length);
  checkHighWatermark();
}

int WatermarkBuffer::read(int fd, uint64_t max_length) {
  int bytes_read = wrapped_buffer_->read(fd, max_length);
  checkHighWatermark();
  return bytes_read;
}

uint64_t WatermarkBuffer::reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) {
  uint64_t bytes_reserved = wrapped_buffer_->reserve(length, iovecs, num_iovecs);
  checkHighWatermark();
  return bytes_reserved;
}

int WatermarkBuffer::write(int fd) {
  int bytes_written = wrapped_buffer_->write(fd);
  checkLowWatermark();
  return bytes_written;
}

void WatermarkBuffer::setWatermarks(uint32_t low_watermark, uint32_t high_watermark) {
  ASSERT(low_watermark < high_watermark);
  low_watermark_ = low_watermark;
  high_watermark_ = high_watermark;
  checkHighWatermark();
  checkLowWatermark();
}

void WatermarkBuffer::checkLowWatermark() {
  if (!above_high_watermark_called_ || wrapped_buffer_->length() >= low_watermark_) {
    return;
  }

  above_high_watermark_called_ = false;
  below_low_watermark_();
}

void WatermarkBuffer::checkHighWatermark() {
  if (above_high_watermark_called_ || high_watermark_ == 0 ||
      wrapped_buffer_->length() <= high_watermark_) {
    return;
  }

  above_high_watermark_called_ = true;
  above_high_watermark_();
}

} // namespace Buffer
} // namespace Envoy
