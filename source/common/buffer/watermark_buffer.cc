#include "source/common/buffer/watermark_buffer.h"

#include <memory>

#include "envoy/buffer/buffer.h"

#include "source/common/common/assert.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Buffer {

void WatermarkBuffer::add(const void* data, uint64_t size) {
  OwnedImpl::add(data, size);
  checkHighAndOverflowWatermarks();
}

void WatermarkBuffer::add(absl::string_view data) {
  OwnedImpl::add(data);
  checkHighAndOverflowWatermarks();
}

void WatermarkBuffer::add(const Instance& data) {
  OwnedImpl::add(data);
  checkHighAndOverflowWatermarks();
}

void WatermarkBuffer::prepend(absl::string_view data) {
  OwnedImpl::prepend(data);
  checkHighAndOverflowWatermarks();
}

void WatermarkBuffer::prepend(Instance& data) {
  OwnedImpl::prepend(data);
  checkHighAndOverflowWatermarks();
}

void WatermarkBuffer::commit(uint64_t length, absl::Span<RawSlice> slices,
                             ReservationSlicesOwnerPtr slices_owner) {
  OwnedImpl::commit(length, slices, std::move(slices_owner));
  checkHighAndOverflowWatermarks();
}

void WatermarkBuffer::drain(uint64_t size) {
  OwnedImpl::drain(size);
  checkLowWatermark();
}

void WatermarkBuffer::move(Instance& rhs) {
  OwnedImpl::move(rhs);
  checkHighAndOverflowWatermarks();
}

void WatermarkBuffer::move(Instance& rhs, uint64_t length) {
  OwnedImpl::move(rhs, length);
  checkHighAndOverflowWatermarks();
}

SliceDataPtr WatermarkBuffer::extractMutableFrontSlice() {
  auto result = OwnedImpl::extractMutableFrontSlice();
  checkLowWatermark();
  return result;
}

// Adjust the reservation size based on space available before hitting
// the high watermark to avoid overshooting by a lot and thus violating the limits
// the watermark is imposing.
Reservation WatermarkBuffer::reserveForRead() {
  constexpr auto preferred_length = default_read_reservation_size_;
  uint64_t adjusted_length = preferred_length;

  if (high_watermark_ > 0 && preferred_length > 0) {
    const uint64_t current_length = OwnedImpl::length();
    if (current_length >= high_watermark_) {
      // Always allow a read of at least some data. The API doesn't allow returning
      // a zero-length reservation.
      adjusted_length = Slice::default_slice_size_;
    } else {
      const uint64_t available_length = high_watermark_ - current_length;
      adjusted_length = IntUtil::roundUpToMultiple(available_length, Slice::default_slice_size_);
      adjusted_length = std::min(adjusted_length, preferred_length);
    }
  }

  return OwnedImpl::reserveWithMaxLength(adjusted_length);
}

void WatermarkBuffer::appendSliceForTest(const void* data, uint64_t size) {
  OwnedImpl::appendSliceForTest(data, size);
  checkHighAndOverflowWatermarks();
}

void WatermarkBuffer::appendSliceForTest(absl::string_view data) {
  appendSliceForTest(data.data(), data.size());
}

void WatermarkBuffer::setWatermarks(uint32_t high_watermark) {
  uint32_t overflow_watermark_multiplier =
      Runtime::getInteger("envoy.buffer.overflow_multiplier", 0);
  if (overflow_watermark_multiplier > 0 &&
      (static_cast<uint64_t>(overflow_watermark_multiplier) * high_watermark) >
          std::numeric_limits<uint32_t>::max()) {
    ENVOY_LOG_MISC(debug, "Error setting overflow threshold: envoy.buffer.overflow_multiplier * "
                          "high_watermark is overflowing. Disabling overflow watermark.");
    overflow_watermark_multiplier = 0;
  }
  low_watermark_ = high_watermark / 2;
  high_watermark_ = high_watermark;
  overflow_watermark_ = overflow_watermark_multiplier * high_watermark;
  checkHighAndOverflowWatermarks();
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

void WatermarkBuffer::checkHighAndOverflowWatermarks() {
  if (high_watermark_ == 0 || OwnedImpl::length() <= high_watermark_) {
    return;
  }

  if (!above_high_watermark_called_) {
    above_high_watermark_called_ = true;
    above_high_watermark_();
  }

  // Check if overflow watermark is enabled, wasn't previously triggered,
  // and the buffer size is above the threshold
  if (overflow_watermark_ != 0 && !above_overflow_watermark_called_ &&
      OwnedImpl::length() > overflow_watermark_) {
    above_overflow_watermark_called_ = true;
    above_overflow_watermark_();
  }
}

BufferMemoryAccountSharedPtr
WatermarkBufferFactory::createAccount(Http::StreamResetHandler* reset_handler) {
  return BufferMemoryAccountImpl::createAccount(this, reset_handler);
}

void WatermarkBufferFactory::onAccountBalanceUpdate(const BufferMemoryAccountSharedPtr& account,
                                                    uint64_t prior_balance) {

  int prev_idx = accountBalanceToClassIndex(prior_balance);
  int new_idx = accountBalanceToClassIndex(account->balance());

  // No need for update, the expected common case.
  if (prev_idx == new_idx) {
    return;
  }

  // Take out of prior bucket if previously tracked.
  BufferMemoryAccountSharedPtr account_in_bucket = nullptr;

  if (prev_idx >= 0) {
    ASSERT(size_class_account_sets_[prev_idx].contains(account));
    // Extract to reuse the existing shared_ptr
    account_in_bucket = std::move(size_class_account_sets_[prev_idx].extract(account).value());
  }

  // Place into new bucket if will track
  if (new_idx >= 0) {
    ASSERT(!size_class_account_sets_[new_idx].contains(account));
    if (account_in_bucket == nullptr) {
      size_class_account_sets_[new_idx].insert(account);
    } else {
      size_class_account_sets_[new_idx].insert(std::move(account_in_bucket));
    }
  }
}

void WatermarkBufferFactory::unregisterAccount(const BufferMemoryAccountSharedPtr& account) {
  int idx = accountBalanceToClassIndex(account->balance());
  if (idx >= 0) {
    ASSERT(size_class_account_sets_[idx].contains(account));
    size_class_account_sets_[idx].erase(account);
  }
}

int WatermarkBufferFactory::accountBalanceToClassIndex(uint64_t balance) {
  uint64_t shifted_balance = balance >> 20; // shift by 1MB.

  if (shifted_balance == 0) {
    return -1; // Not worth tracking anything < 1MB.
  }
  int class_idx = 0;
  shifted_balance >>= 1;

  const int last_idx = size_class_account_sets_.size() - 1;
  while (shifted_balance > 0 && class_idx < last_idx) {
    shifted_balance >>= 1;
    ++class_idx;
  }

  return class_idx;
}

} // namespace Buffer
} // namespace Envoy
