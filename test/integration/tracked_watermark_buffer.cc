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
        total_buffer_size_ = total_buffer_size_ + current_size - buffer_info.current_size_;
        if (buffer_info.max_size_ < current_size) {
          buffer_info.max_size_ = current_size;
        }
        buffer_info.current_size_ = current_size;

        checkIfExpectedBalancesMet();
      },
      [this, &buffer_info](uint32_t watermark) {
        absl::MutexLock lock(&mutex_);
        buffer_info.watermark_ = watermark;
      },
      [this, &buffer_info](TrackedWatermarkBuffer* buffer) {
        absl::MutexLock lock(&mutex_);
        ASSERT(active_buffer_count_ > 0);
        --active_buffer_count_;
        total_buffer_size_ -= buffer_info.current_size_;
        buffer_info.current_size_ = 0;

        // Remove bound account tracking.
        auto account = buffer->getAccountForTest();
        if (account) {
          auto& set = account_infos_[account];
          // Erase buffer, one entry should be removed.
          ASSERT(set.erase(buffer) == 1);
          ASSERT(actively_bound_buffers_.erase(buffer) == 1);

          // Erase account entry if there are no active bound buffers, and
          // there's no other pointers to the account besides the local account
          // pointer and within the map.
          //
          // It's possible for an account to no longer be bound to a buffer in
          // the case that the H2 stream completes, but the data hasn't flushed
          // at TCP.
          if (set.empty() && account.use_count() == 2) {
            ASSERT(account_infos_.erase(account) == 1);
          }
        }
      },
      [this](BufferMemoryAccountSharedPtr& account, TrackedWatermarkBuffer* buffer) {
        absl::MutexLock lock(&mutex_);
        // Buffers should only be bound once.
        ASSERT(actively_bound_buffers_.find(buffer) == actively_bound_buffers_.end());
        account_infos_[account].emplace(buffer);
        actively_bound_buffers_.emplace(buffer);
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

uint64_t TrackedWatermarkBufferFactory::totalBufferSize() const {
  absl::MutexLock lock(&mutex_);
  return total_buffer_size_;
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

bool TrackedWatermarkBufferFactory::waitUntilTotalBufferedExceeds(
    uint64_t byte_size, std::chrono::milliseconds timeout) {
  absl::MutexLock lock(&mutex_);
  auto predicate = [this, byte_size]() ABSL_SHARED_LOCKS_REQUIRED(mutex_) {
    mutex_.AssertHeld();
    return total_buffer_size_ >= byte_size;
  };
  return mutex_.AwaitWithTimeout(absl::Condition(&predicate), absl::Milliseconds(timeout.count()));
}

void TrackedWatermarkBufferFactory::removeDanglingAccounts() {
  auto accounts_it = account_infos_.begin();
  while (accounts_it != account_infos_.end()) {
    auto next = std::next(accounts_it);

    // Remove all "dangling" accounts.
    if (accounts_it->first.use_count() == 1) {
      account_infos_.erase(accounts_it);
    }

    accounts_it = next;
  }
}

void TrackedWatermarkBufferFactory::setExpectedAccountBalance(uint64_t byte_size,
                                                              uint32_t num_accounts) {
  absl::MutexLock lock(&mutex_);
  ASSERT(!expected_balances_.has_value());
  expected_balances_.emplace(byte_size, num_accounts);
}

bool TrackedWatermarkBufferFactory::waitForExpectedAccountBalanceWithTimeout(
    std::chrono::milliseconds timeout) {
  return expected_balances_met_.WaitForNotificationWithTimeout(absl::FromChrono(timeout));
}

void TrackedWatermarkBufferFactory::checkIfExpectedBalancesMet() {
  if (!expected_balances_ || expected_balances_met_.HasBeenNotified()) {
    return;
  }

  removeDanglingAccounts();
  if (account_infos_.size() < expected_balances_->num_accounts_) {
    return;
  }

  // This is thread safe since this function should run on the only Envoy worker
  // thread.
  for (auto& acc : account_infos_) {
    if (static_cast<Buffer::BufferMemoryAccountImpl*>(acc.first.get())->balance() <
        expected_balances_->balance_per_account_) {
      return;
    }
  }

  expected_balances_met_.Notify();
}

uint64_t TrackedWatermarkBufferFactory::numAccountsActive() {
  absl::MutexLock lock(&mutex_);
  removeDanglingAccounts();
  return account_infos_.size();
}

uint64_t TrackedWatermarkBufferFactory::numBuffersActivelyBound() const {
  absl::MutexLock lock(&mutex_);
  return actively_bound_buffers_.size();
}

} // namespace Buffer
} // namespace Envoy
