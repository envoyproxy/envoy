#include "test/integration/tracked_watermark_buffer.h"

namespace Envoy {
namespace Buffer {

TrackedWatermarkBufferFactory::~TrackedWatermarkBufferFactory() {
  ASSERT(active_buffer_count_ == 0);
}

Buffer::InstancePtr
TrackedWatermarkBufferFactory::create(std::function<void()> below_low_watermark,
                                      std::function<void()> above_high_watermark,
                                      std::function<void()> above_overflow_watermark) {
  absl::MutexLock lock(&mutex_);
  uint64_t idx = next_idx_++;
  ++active_buffer_count_;
  BufferInfo& buffer_info = buffer_infos_[idx];
  return std::make_unique<TrackedWatermarkBuffer>(
      [this, &buffer_info](uint64_t current_size) {
        absl::MutexLock lock(&mutex_);
        if (buffer_info.max_size_ < current_size) {
          buffer_info.max_size_ = current_size;
        }
      },
      [this, &buffer_info](uint32_t watermark) {
        absl::MutexLock lock(&mutex_);
        buffer_info.watermark_ = watermark;
      },
      [this]() {
        absl::MutexLock lock(&mutex_);
        ASSERT(active_buffer_count_ > 0);
        --active_buffer_count_;
      },
      below_low_watermark, above_high_watermark, above_overflow_watermark);
}

uint64_t TrackedWatermarkBufferFactory::numBuffersCreated() const {
  absl::MutexLock lock(&mutex_);
  return buffer_infos_.size();
}

uint64_t TrackedWatermarkBufferFactory::numBuffersActive() const {
  absl::MutexLock lock(&mutex_);
  return active_buffer_count_;
}

uint64_t TrackedWatermarkBufferFactory::maxBufferSize() const {
  absl::MutexLock lock(&mutex_);
  uint64_t val = 0;
  for (auto& item : buffer_infos_) {
    val = std::max(val, item.second.max_size_);
  }
  return val;
}

uint64_t TrackedWatermarkBufferFactory::sumMaxBufferSizes() const {
  absl::MutexLock lock(&mutex_);
  uint64_t val = 0;
  for (auto& item : buffer_infos_) {
    val += item.second.max_size_;
  }
  return val;
}

std::pair<uint32_t, uint32_t> TrackedWatermarkBufferFactory::highWatermarkRange() const {
  absl::MutexLock lock(&mutex_);
  uint32_t min_watermark = 0;
  uint32_t max_watermark = 0;
  bool watermarks_set = false;

  for (auto& item : buffer_infos_) {
    uint32_t watermark = item.second.watermark_;
    if (watermark == 0) {
      max_watermark = 0;
      watermarks_set = true;
    } else {
      if (watermarks_set) {
        if (min_watermark != 0) {
          min_watermark = std::min(min_watermark, watermark);
        } else {
          min_watermark = watermark;
        }
        if (max_watermark != 0) {
          max_watermark = std::max(max_watermark, watermark);
        }
      } else {
        watermarks_set = true;
        min_watermark = watermark;
        max_watermark = watermark;
      }
    }
  }

  return std::make_pair(min_watermark, max_watermark);
}

} // namespace Buffer
} // namespace Envoy
