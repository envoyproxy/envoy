#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/server/instance.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/watermark_buffer.h"

#include "test/test_common/utility.h"

#include "absl/container/node_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"

namespace Envoy {
namespace Buffer {

// WatermarkBuffer subclass that hooks into updates to buffer size and buffer high watermark config.
class TrackedWatermarkBuffer : public Buffer::WatermarkBuffer {
public:
  TrackedWatermarkBuffer(
      std::function<void(uint64_t current_size)> update_size,
      std::function<void(uint64_t watermark)> update_high_watermark,
      std::function<void(TrackedWatermarkBuffer*)> on_delete,
      std::function<void(BufferMemoryAccountSharedPtr&, TrackedWatermarkBuffer*)> on_bind,
      std::function<void()> below_low_watermark, std::function<void()> above_high_watermark,
      std::function<void()> above_overflow_watermark)
      : WatermarkBuffer(below_low_watermark, above_high_watermark, above_overflow_watermark),
        update_size_(update_size), update_high_watermark_(update_high_watermark),
        on_delete_(on_delete), on_bind_(on_bind) {}
  ~TrackedWatermarkBuffer() override { on_delete_(this); }

  void setWatermarks(uint64_t watermark, uint32_t overload) override {
    update_high_watermark_(watermark);
    WatermarkBuffer::setWatermarks(watermark, overload);
  }

  void bindAccount(BufferMemoryAccountSharedPtr account) override {
    on_bind_(account, this);
    WatermarkBuffer::bindAccount(account);
  }

protected:
  void checkHighAndOverflowWatermarks() override {
    update_size_(length());
    WatermarkBuffer::checkHighAndOverflowWatermarks();
  }

  void checkLowWatermark() override {
    update_size_(length());
    WatermarkBuffer::checkLowWatermark();
  }

private:
  std::function<void(uint64_t current_size)> update_size_;
  std::function<void(uint64_t)> update_high_watermark_;
  std::function<void(TrackedWatermarkBuffer*)> on_delete_;
  std::function<void(BufferMemoryAccountSharedPtr&, TrackedWatermarkBuffer*)> on_bind_;
};

// Factory that tracks how the created buffers are used.
class TrackedWatermarkBufferFactory : public WatermarkBufferFactory {
public:
  // Use the default minimum tracking threshold.
  TrackedWatermarkBufferFactory();
  TrackedWatermarkBufferFactory(uint32_t min_tracking_bytes);
  ~TrackedWatermarkBufferFactory() override;
  // Buffer::WatermarkFactory
  Buffer::InstancePtr createBuffer(std::function<void()> below_low_watermark,
                                   std::function<void()> above_high_watermark,
                                   std::function<void()> above_overflow_watermark) override;
  BufferMemoryAccountSharedPtr createAccount(Http::StreamResetHandler& reset_handler) override;
  void unregisterAccount(const BufferMemoryAccountSharedPtr& account,
                         absl::optional<uint32_t> current_class) override;

  // Number of buffers created.
  uint64_t numBuffersCreated() const;
  // Number of buffers still in use.
  uint64_t numBuffersActive() const;
  // Total bytes buffered.
  uint64_t totalBufferSize() const;
  // Size of the largest buffer.
  uint64_t maxBufferSize() const;
  // Sum of the max size of all known buffers.
  uint64_t sumMaxBufferSizes() const;
  // Get lower and upper bound on buffer high watermarks. A watermark of 0 indicates that watermark
  // functionality is disabled.
  std::pair<uint64_t, uint64_t> highWatermarkRange() const;

  // Number of accounts created.
  uint64_t numAccountsCreated() const;

  // Waits for the expected number of accounts unregistered. Unlike
  // numAccountsCreated, there are no pre-existing hooks into Envoy when an
  // account unregistered call occurs as it depends upon deferred delete.
  // This creates the synchronization needed.
  bool waitForExpectedAccountUnregistered(
      uint64_t expected_accounts_unregistered,
      std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  // Total bytes currently buffered across all known buffers.
  uint64_t totalBytesBuffered() const {
    absl::MutexLock lock(mutex_);
    return total_buffer_size_;
  }

  // Wait until total bytes buffered exceeds the a given size.
  bool
  waitUntilTotalBufferedExceeds(uint64_t byte_size,
                                std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  // Set the expected account balance, prior to sending requests.
  // The test thread can then wait for this condition to be true.
  // This is separated so that the test thread can spin up requests however it
  // desires in between.
  //
  // The Envoy worker thread will notify the test thread once the condition is
  // met.
  void setExpectedAccountBalance(uint64_t byte_size_per_account, uint32_t num_accounts);
  bool waitForExpectedAccountBalanceWithTimeout(
      std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  // Wait for the expected number of accounts and number of bound buffers.
  //
  // Due to deferred deletion, it possible that the Envoy hasn't cleaned up on
  // its end, but the stream has been completed. This avoids that by awaiting
  // for the side effect of the deletion to have occurred.
  bool waitUntilExpectedNumberOfAccountsAndBoundBuffers(
      uint32_t num_accounts, uint32_t num_bound_buffers,
      std::chrono::milliseconds timeout = TestUtility::DefaultTimeout);

  using AccountToBoundBuffersMap =
      absl::flat_hash_map<BufferMemoryAccountSharedPtr,
                          absl::flat_hash_set<TrackedWatermarkBuffer*>>;
  // Used to inspect all accounts tied to any buffer created from this factory.
  void inspectAccounts(std::function<void(AccountToBoundBuffersMap&)> func,
                       Server::Instance& server);

  // Used to inspect the memory class to accounts within that class structure.
  // This differs from inspectAccounts as that has all accounts bounded to an
  // active buffer, while this might not track certain accounts (e.g. below
  // thresholds.) As implemented this is NOT thread-safe!
  void inspectMemoryClasses(
      std::function<void(WatermarkBufferFactory::MemoryClassesToAccountsSet&)> func);

private:
  // Remove "dangling" accounts; accounts where the account_info map is the only
  // entity still pointing to the account.
  void removeDanglingAccounts() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  // Should only be executed on the Envoy's worker thread. Otherwise, we have a
  // possible race condition.
  void checkIfExpectedBalancesMet() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  struct BufferInfo {
    uint64_t watermark_ = 0;
    uint64_t current_size_ = 0;
    uint64_t max_size_ = 0;
  };

  struct AccountBalanceExpectations {
    AccountBalanceExpectations(uint64_t balance_per_account, uint32_t num_accounts)
        : balance_per_account_(balance_per_account), num_accounts_(num_accounts) {}

    uint64_t balance_per_account_ = 0;
    uint32_t num_accounts_ = 0;
  };

  mutable absl::Mutex mutex_;
  // Id of the next buffer to create.
  uint64_t next_idx_ ABSL_GUARDED_BY(mutex_) = 0;
  // Number of buffers currently in existence.
  uint64_t active_buffer_count_ ABSL_GUARDED_BY(mutex_) = 0;
  // total bytes buffered across all buffers.
  uint64_t total_buffer_size_ ABSL_GUARDED_BY(mutex_) = 0;
  // total number of accounts created
  uint64_t total_accounts_created_ ABSL_GUARDED_BY(mutex_) = 0;
  // total number of accounts unregistered
  uint64_t total_accounts_unregistered_ ABSL_GUARDED_BY(mutex_) = 0;
  // Info about the buffer, by buffer idx.
  absl::node_hash_map<uint64_t, BufferInfo> buffer_infos_ ABSL_GUARDED_BY(mutex_);
  // The expected balances for the accounts. If set, when a buffer updates its
  // size, it also checks whether the expected_balances has been satisfied, and
  // notifies waiters of expected_balances_met_.
  absl::optional<AccountBalanceExpectations> expected_balances_ ABSL_GUARDED_BY(mutex_);
  absl::Notification expected_balances_met_;
  // Map from accounts to buffers bound to that account.
  AccountToBoundBuffersMap account_infos_ ABSL_GUARDED_BY(mutex_);
  // Set of actively bound buffers. Used for asserting that buffers are bound
  // only once.
  absl::flat_hash_set<TrackedWatermarkBuffer*> actively_bound_buffers_ ABSL_GUARDED_BY(mutex_);
};

} // namespace Buffer
} // namespace Envoy
