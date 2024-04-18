#pragma once

#include <functional>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/optref.h"
#include "envoy/config/overload/v3/overload.pb.h"

#include "source/common/buffer/buffer_impl.h"

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
  size_t addFragments(absl::Span<const absl::string_view> fragments) override;
  void drain(uint64_t size) override;
  void move(Instance& rhs) override;
  void move(Instance& rhs, uint64_t length) override;
  void move(Instance& rhs, uint64_t length, bool reset_drain_trackers_and_accounting) override;
  SliceDataPtr extractMutableFrontSlice() override;
  Reservation reserveForRead() override;
  void postProcess() override { checkLowWatermark(); }
  void appendSliceForTest(const void* data, uint64_t size) override;
  void appendSliceForTest(absl::string_view data) override;

  void setWatermarks(uint32_t high_watermark, uint32_t overflow_watermark = 0) override;
  uint32_t highWatermark() const override { return high_watermark_; }
  // Returns true if the high watermark callbacks have been called more recently
  // than the low watermark callbacks.
  bool highWatermarkTriggered() const override { return above_high_watermark_called_; }

protected:
  virtual void checkHighAndOverflowWatermarks();
  virtual void checkLowWatermark();

private:
  void commit(uint64_t length, absl::Span<RawSlice> slices,
              ReservationSlicesOwnerPtr slices_owner) override;

  std::function<void()> below_low_watermark_;
  std::function<void()> above_high_watermark_;
  std::function<void()> above_overflow_watermark_;

  // Used for enforcing buffer limits (off by default). If these are set to non-zero by a call to
  // setWatermarks() the watermark callbacks will be called as described above.
  uint32_t high_watermark_{0};
  uint32_t low_watermark_{0};
  uint32_t overflow_watermark_{0};
  // Tracks the latest state of watermark callbacks.
  // True between the time above_high_watermark_ has been called until below_low_watermark_ has
  // been called.
  bool above_high_watermark_called_{false};
  // Set to true when above_overflow_watermark_ is called (and isn't cleared).
  bool above_overflow_watermark_called_{false};
};

using WatermarkBufferPtr = std::unique_ptr<WatermarkBuffer>;

class WatermarkBufferFactory;

/**
 * A BufferMemoryAccountImpl tracks allocated bytes across associated buffers and
 * slices that originate from those buffers, or are untagged and pass through an
 * associated buffer.
 *
 * This BufferMemoryAccount is produced by the *WatermarkBufferFactory*.
 */
class BufferMemoryAccountImpl : public BufferMemoryAccount {
public:
  // Used to create the account, and complete wiring with the factory
  // and shared_this_.
  static BufferMemoryAccountSharedPtr createAccount(WatermarkBufferFactory* factory,
                                                    Http::StreamResetHandler& reset_handler);
  ~BufferMemoryAccountImpl() override {
    // The buffer_memory_allocated_ should always be zero on destruction, even
    // if we triggered a reset of the downstream. This is because the destructor
    // will only trigger when no entities have a pointer to the account, meaning
    // any slices which charge and credit the account should have credited the
    // account when they were deleted, maintaining this invariant.
    ASSERT(buffer_memory_allocated_ == 0);
    ASSERT(!reset_handler_.has_value());
  }

  // Make not copyable
  BufferMemoryAccountImpl(const BufferMemoryAccountImpl&) = delete;
  BufferMemoryAccountImpl& operator=(const BufferMemoryAccountImpl&) = delete;

  // Make not movable.
  BufferMemoryAccountImpl(BufferMemoryAccountImpl&&) = delete;
  BufferMemoryAccountImpl& operator=(BufferMemoryAccountImpl&&) = delete;

  uint64_t balance() const { return buffer_memory_allocated_; }
  void charge(uint64_t amount) override;
  void credit(uint64_t amount) override;

  // Clear the associated downstream, preparing the account to be destroyed.
  // This is idempotent.
  void clearDownstream() override;

  void resetDownstream() override {
    if (reset_handler_.has_value()) {
      reset_handler_->resetStream(Http::StreamResetReason::OverloadManager);
    }
  }

  // The number of memory classes the Account expects to exists. See
  // *WatermarkBufferFactory* for details on the memory classes.
  static constexpr uint32_t NUM_MEMORY_CLASSES_ = 8;

private:
  BufferMemoryAccountImpl(WatermarkBufferFactory* factory, Http::StreamResetHandler& reset_handler)
      : factory_(factory), reset_handler_(reset_handler) {}

  // Returns the class index based off of the buffer_memory_allocated_
  // This can differ with current_bucket_idx_ if buffer_memory_allocated_ was
  // just modified.
  // Returned class index, if present, is in the range [0, NUM_MEMORY_CLASSES_).
  absl::optional<uint32_t> balanceToClassIndex();
  void updateAccountClass();

  uint64_t buffer_memory_allocated_ = 0;
  // Current bucket index where the account is being tracked in.
  absl::optional<uint32_t> current_bucket_idx_{};

  WatermarkBufferFactory* factory_ = nullptr;

  OptRef<Http::StreamResetHandler> reset_handler_;
  // Keep a copy of the shared_ptr pointing to this account. We opted to go this
  // route rather than enable_shared_from_this to avoid wasteful atomic
  // operations e.g. when updating the tracking of the account.
  // This is set through the createAccount static method which is the only way to
  // instantiate an instance of this class. This should is cleared when
  // unregistering from the factory.
  BufferMemoryAccountSharedPtr shared_this_ = nullptr;
};

/**
 * The WatermarkBufferFactory creates *WatermarkBuffer*s and
 * *BufferMemoryAccountImpl* that can be used to bind to the created buffers
 * from a given downstream (and corresponding upstream, if one exists). The
 * accounts can then be used to reset the underlying stream.
 *
 * Any account produced by this factory might be tracked by the factory using the
 * following scheme:
 *
 * 1) Is the account balance >= 1MB? If not don't track.
 * 2) For all accounts above the minimum threshold for tracking, put the account
 *    into one of the *BufferMemoryAccountImpl::NUM_MEMORY_CLASSES_* buckets.
 *
 *    We keep buckets containing accounts within a "memory class", which are
 *    power of two buckets. For example, with a minimum threshold of 1MB, our
 *    first bucket contains [1MB, 2MB) accounts, the second bucket contains
 *    [2MB, 4MB), and so forth for
 *    *BufferMemoryAccountImpl::NUM_MEMORY_CLASSES_* buckets. These buckets
 *    allow us to coarsely track accounts, and if overloaded we can easily
 *    target more expensive streams.
 *
 *    As the account balance changes, the account informs the Watermark Factory
 *    if the bucket for that account has changed. See
 *    *BufferMemoryAccountImpl::balanceToClassIndex()* for details on the memory
 *    class for a given account balance.
 *
 * TODO(kbaichoo): Update this documentation when we make the minimum account
 * threshold configurable.
 *
 */
class WatermarkBufferFactory : public WatermarkFactory {
public:
  WatermarkBufferFactory(const envoy::config::overload::v3::BufferFactoryConfig& config);

  // Buffer::WatermarkFactory
  ~WatermarkBufferFactory() override;
  InstancePtr createBuffer(std::function<void()> below_low_watermark,
                           std::function<void()> above_high_watermark,
                           std::function<void()> above_overflow_watermark) override {
    return std::make_unique<WatermarkBuffer>(below_low_watermark, above_high_watermark,
                                             above_overflow_watermark);
  }

  BufferMemoryAccountSharedPtr createAccount(Http::StreamResetHandler& reset_handler) override;
  uint64_t resetAccountsGivenPressure(float pressure) override;

  // Called by BufferMemoryAccountImpls created by the factory on account class
  // updated.
  void updateAccountClass(const BufferMemoryAccountSharedPtr& account,
                          absl::optional<uint32_t> current_class,
                          absl::optional<uint32_t> new_class);

  uint32_t bitshift() const { return bitshift_; }

  // Unregister a buffer memory account.
  virtual void unregisterAccount(const BufferMemoryAccountSharedPtr& account,
                                 absl::optional<uint32_t> current_class);

protected:
  // Enable subclasses to inspect the mapping.
  using MemoryClassesToAccountsSet = std::array<absl::flat_hash_set<BufferMemoryAccountSharedPtr>,
                                                BufferMemoryAccountImpl::NUM_MEMORY_CLASSES_>;
  MemoryClassesToAccountsSet size_class_account_sets_;
  // How much to bit shift right balances to test whether the account should be
  // tracked in *size_class_account_sets_*.
  const uint32_t bitshift_;
};

} // namespace Buffer
} // namespace Envoy
