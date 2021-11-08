#include "test/integration/tracked_watermark_buffer.h"

#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/thread/thread.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/thread_local/thread_local_object.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Buffer {

TrackedWatermarkBufferFactory::TrackedWatermarkBufferFactory() : TrackedWatermarkBufferFactory(0) {}

TrackedWatermarkBufferFactory::TrackedWatermarkBufferFactory(
    uint32_t minimum_account_to_track_power_of_two)
    : WatermarkBufferFactory([minimum_account_to_track_power_of_two]() {
        auto config = envoy::config::overload::v3::BufferFactoryConfig();
        if (minimum_account_to_track_power_of_two > 0) {
          config.set_minimum_account_to_track_power_of_two(minimum_account_to_track_power_of_two);
        }
        return config;
      }()) {}

TrackedWatermarkBufferFactory::~TrackedWatermarkBufferFactory() {
  ASSERT(active_buffer_count_ == 0);
}

Buffer::InstancePtr
TrackedWatermarkBufferFactory::createBuffer(std::function<void()> below_low_watermark,
                                            std::function<void()> above_high_watermark,
                                            std::function<void()> above_overflow_watermark) {
  absl::MutexLock lock(&mutex_);
  uint64_t idx = next_idx_++;
  ++active_buffer_count_;
  BufferInfo& buffer_info = buffer_infos_[idx];
  return std::make_unique<TrackedWatermarkBuffer>(
      // update_size
      [this, &buffer_info](uint64_t current_size) {
        absl::MutexLock lock(&mutex_);
        total_buffer_size_ = total_buffer_size_ + current_size - buffer_info.current_size_;
        if (buffer_info.max_size_ < current_size) {
          buffer_info.max_size_ = current_size;
        }
        buffer_info.current_size_ = current_size;

        checkIfExpectedBalancesMet();
      },
      // update_high_watermark
      [this, &buffer_info](uint32_t watermark) {
        absl::MutexLock lock(&mutex_);
        buffer_info.watermark_ = watermark;
      },
      // on_delete
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
          RELEASE_ASSERT(set.erase(buffer) == 1, "Expected to remove buffer from account_infos.");
          RELEASE_ASSERT(actively_bound_buffers_.erase(buffer) == 1,
                         "Did not find buffer in actively_bound_buffers_.");
          // Erase account entry if there are no active bound buffers, and
          // there's no other pointers to the account besides the local account
          // pointer and within the map.
          //
          // It's possible for an account to no longer be bound to a buffer in
          // the case that the H2 stream completes, but the data hasn't flushed
          // at TCP.
          if (set.empty() && account.use_count() == 2) {
            RELEASE_ASSERT(account_infos_.erase(account) == 1,
                           "Expected to remove account from account_infos.");
          }
        }
      },
      // on_bind
      [this](BufferMemoryAccountSharedPtr& account, TrackedWatermarkBuffer* buffer) {
        absl::MutexLock lock(&mutex_);
        // Only track non-null accounts.
        if (account) {
          account_infos_[account].emplace(buffer);
          actively_bound_buffers_.emplace(buffer);
        }
      },
      below_low_watermark, above_high_watermark, above_overflow_watermark);
}

BufferMemoryAccountSharedPtr
TrackedWatermarkBufferFactory::createAccount(Http::StreamResetHandler& reset_handler) {
  auto account = WatermarkBufferFactory::createAccount(reset_handler);
  if (account != nullptr) {
    absl::MutexLock lock(&mutex_);
    ++total_accounts_created_;
  }
  return account;
}

void TrackedWatermarkBufferFactory::unregisterAccount(const BufferMemoryAccountSharedPtr& account,
                                                      absl::optional<uint32_t> current_class) {
  WatermarkBufferFactory::unregisterAccount(account, current_class);
  absl::MutexLock lock(&mutex_);
  ++total_accounts_unregistered_;
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

uint64_t TrackedWatermarkBufferFactory::numAccountsCreated() const {
  absl::MutexLock lock(&mutex_);
  return total_accounts_created_;
}

bool TrackedWatermarkBufferFactory::waitForExpectedAccountUnregistered(
    uint64_t expected_accounts_unregistered, std::chrono::milliseconds timeout) {
  absl::MutexLock lock(&mutex_);
  auto predicate = [this, expected_accounts_unregistered]() ABSL_SHARED_LOCKS_REQUIRED(mutex_) {
    mutex_.AssertHeld();
    return expected_accounts_unregistered == total_accounts_unregistered_;
  };
  return mutex_.AwaitWithTimeout(absl::Condition(&predicate), absl::Milliseconds(timeout.count()));
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
      ASSERT(accounts_it->second.empty());
      account_infos_.erase(accounts_it);
    }

    accounts_it = next;
  }
}

void TrackedWatermarkBufferFactory::inspectAccounts(
    std::function<void(AccountToBoundBuffersMap&)> func, Server::Instance& server) {
  absl::Notification done_notification;
  ThreadLocal::TypedSlotPtr<> slot;
  Envoy::Thread::ThreadId main_tid;

  server.dispatcher().post([&] {
    slot = ThreadLocal::TypedSlot<>::makeUnique(server.threadLocal());
    slot->set(
        [](Envoy::Event::Dispatcher&) -> std::shared_ptr<Envoy::ThreadLocal::ThreadLocalObject> {
          return nullptr;
        });

    main_tid = server.api().threadFactory().currentThreadId();

    slot->runOnAllThreads(
        [main_tid, &server, &func, this](OptRef<ThreadLocal::ThreadLocalObject>) {
          // Run on the worker thread.
          if (server.api().threadFactory().currentThreadId() != main_tid) {
            absl::MutexLock lock(&(this->mutex_));
            func(this->account_infos_);
          }
        },
        [&slot, &done_notification] {
          slot.reset(nullptr);
          done_notification.Notify();
        });
  });

  done_notification.WaitForNotification();
}

void TrackedWatermarkBufferFactory::inspectMemoryClasses(
    std::function<void(MemoryClassesToAccountsSet&)> func) {
  func(size_class_account_sets_);
}

void TrackedWatermarkBufferFactory::setExpectedAccountBalance(uint64_t byte_size_per_account,
                                                              uint32_t num_accounts) {
  absl::MutexLock lock(&mutex_);
  ASSERT(!expected_balances_.has_value());
  expected_balances_.emplace(byte_size_per_account, num_accounts);
}

bool TrackedWatermarkBufferFactory::waitForExpectedAccountBalanceWithTimeout(
    std::chrono::milliseconds timeout) {
  return expected_balances_met_.WaitForNotificationWithTimeout(absl::FromChrono(timeout));
}

bool TrackedWatermarkBufferFactory::waitUntilExpectedNumberOfAccountsAndBoundBuffers(
    uint32_t num_accounts, uint32_t num_bound_buffers, std::chrono::milliseconds timeout) {
  absl::MutexLock lock(&mutex_);
  auto predicate = [this, num_accounts, num_bound_buffers]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    mutex_.AssertHeld();
    removeDanglingAccounts();
    return num_bound_buffers == actively_bound_buffers_.size() &&
           num_accounts == account_infos_.size();
  };
  return mutex_.AwaitWithTimeout(absl::Condition(&predicate), absl::FromChrono(timeout));
}

void TrackedWatermarkBufferFactory::checkIfExpectedBalancesMet() {
  if (!expected_balances_ || expected_balances_met_.HasBeenNotified()) {
    return;
  }

  removeDanglingAccounts();

  if (account_infos_.size() == expected_balances_->num_accounts_) {
    // This is thread safe since this function should run on the only Envoy worker
    // thread.
    for (auto& acc : account_infos_) {
      if (static_cast<BufferMemoryAccountImpl*>(acc.first.get())->balance() <
          expected_balances_->balance_per_account_) {
        return;
      }
    }

    expected_balances_met_.Notify();
  }
}

} // namespace Buffer
} // namespace Envoy
