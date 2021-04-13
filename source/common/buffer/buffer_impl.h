#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"

#include "common/common/assert.h"
#include "common/common/non_copyable.h"
#include "common/common/utility.h"
#include "common/event/libevent.h"

namespace Envoy {
namespace Buffer {

/**
 * A Slice manages a contiguous block of bytes.
 * The block is arranged like this:
 *                   |<- dataSize() ->|<- reservableSize() ->|
 * +-----------------+----------------+----------------------+
 * | Drained         | Data           | Reservable           |
 * | Unused space    | Usable content | New content can be   |
 * | that formerly   |                | added here with      |
 * | was in the Data |                | reserve()/commit()   |
 * | section         |                | or append()          |
 * +-----------------+----------------+----------------------+
 * ^                 ^                ^                      ^
 * |                 |                |                      |
 * base_             data()           base_ + reservable_    base_ + capacity_
 */
class Slice {
public:
  using Reservation = RawSlice;
  using StoragePtr = std::unique_ptr<uint8_t[]>;

  static constexpr uint32_t free_list_max_ = Buffer::Reservation::MAX_SLICES_;
  using FreeListType = absl::InlinedVector<StoragePtr, free_list_max_>;
  class FreeListReference {
  private:
    FreeListReference(FreeListType& free_list) : free_list_(free_list) {}
    FreeListType& free_list_;
    friend class Slice;
  };

  /**
   * Create an empty Slice with 0 capacity.
   */
  Slice() = default;

  /**
   * Create an empty mutable Slice that owns its storage, which it charges to
   * the provided account, if any.
   * @param min_capacity number of bytes of space the slice should have. Actual capacity is rounded
   * up to the next multiple of 4kb.
   * @param account the account to charge.
   * @param freelist to search for the backing storage, if any.
   */
  Slice(uint64_t min_capacity, BufferMemoryAccountSharedPtr account,
        absl::optional<FreeListReference> free_list = absl::nullopt)
      : capacity_(sliceSize(min_capacity)), storage_(newStorage(capacity_, free_list)),
        base_(storage_.get()), data_(0), reservable_(0) {
    if (account) {
      account->charge(capacity_);
      account_ = account;
    }
  }

  /**
   * Create an immutable Slice that refers to an external buffer fragment.
   * @param fragment provides externally owned immutable data.
   */
  Slice(BufferFragment& fragment)
      : capacity_(fragment.size()), storage_(nullptr),
        base_(static_cast<uint8_t*>(const_cast<void*>(fragment.data()))), data_(0),
        reservable_(fragment.size()) {
    addDrainTracker([&fragment]() { fragment.done(); });
  }

  Slice(Slice&& rhs) noexcept {
    storage_ = std::move(rhs.storage_);
    drain_trackers_ = std::move(rhs.drain_trackers_);
    account_ = std::move(rhs.account_);
    base_ = rhs.base_;
    data_ = rhs.data_;
    reservable_ = rhs.reservable_;
    capacity_ = rhs.capacity_;

    rhs.base_ = nullptr;
    rhs.data_ = 0;
    rhs.reservable_ = 0;
    rhs.capacity_ = 0;
  }

  Slice& operator=(Slice&& rhs) noexcept {
    if (this != &rhs) {
      callAndClearDrainTrackersAndCharges();

      freeStorage(std::move(storage_), capacity_);
      storage_ = std::move(rhs.storage_);
      drain_trackers_ = std::move(rhs.drain_trackers_);
      account_ = std::move(rhs.account_);
      base_ = rhs.base_;
      data_ = rhs.data_;
      reservable_ = rhs.reservable_;
      capacity_ = rhs.capacity_;

      rhs.base_ = nullptr;
      rhs.data_ = 0;
      rhs.reservable_ = 0;
      rhs.capacity_ = 0;
    }

    return *this;
  }

  ~Slice() {
    callAndClearDrainTrackersAndCharges();
    freeStorage(std::move(storage_), capacity_);
  }

  void freeStorage(FreeListReference free_list) {
    callAndClearDrainTrackersAndCharges();
    freeStorage(std::move(storage_), capacity_, free_list);
  }

  /**
   * @return true if the data in the slice is mutable
   */
  bool isMutable() const { return storage_ != nullptr; }

  /**
   * @return true if content in this Slice can be coalesced into another Slice.
   */
  bool canCoalesce() const { return storage_ != nullptr; }

  /**
   * @return a pointer to the start of the usable content.
   */
  const uint8_t* data() const { return base_ + data_; }

  /**
   * @return a pointer to the start of the usable content.
   */
  uint8_t* data() { return base_ + data_; }

  /**
   * @return the size in bytes of the usable content.
   */
  uint64_t dataSize() const { return reservable_ - data_; }

  /**
   * Remove the first `size` bytes of usable content. Runs in O(1) time.
   * @param size number of bytes to remove. If greater than data_size(), the result is undefined.
   */
  void drain(uint64_t size) {
    ASSERT(data_ + size <= reservable_);
    data_ += size;
    if (data_ == reservable_) {
      // All the data in the slice has been drained. Reset the offsets so all
      // the data can be reused.
      data_ = 0;
      reservable_ = 0;
    }
  }

  /**
   * @return the number of bytes available to be reserve()d.
   * @note Read-only implementations of Slice should return zero from this method.
   */
  uint64_t reservableSize() const {
    ASSERT(capacity_ >= reservable_);
    return capacity_ - reservable_;
  }

  /**
   * Reserve `size` bytes that the caller can populate with content. The caller SHOULD then
   * call commit() to add the newly populated content from the Reserved section to the Data
   * section.
   * @note If there is already an outstanding reservation (i.e., a reservation obtained
   *       from reserve() that has not been released by calling commit()), this method will
   *       return a new reservation that replaces it.
   * @param size the number of bytes to reserve. The Slice implementation MAY reserve
   *        fewer bytes than requested (for example, if it doesn't have enough room in the
   *        Reservable section to fulfill the whole request).
   * @return a tuple containing the address of the start of resulting reservation and the
   *         reservation size in bytes. If the address is null, the reservation failed.
   * @note Read-only implementations of Slice should return {nullptr, 0} from this method.
   */
  Reservation reserve(uint64_t size) {
    if (size == 0) {
      return {nullptr, 0};
    }
    // Verify the semantics that drain() enforces: if the slice is empty, either because
    // no data has been added or because all the added data has been drained, the data
    // section is at the very start of the slice.
    ASSERT(!(dataSize() == 0 && data_ > 0));
    uint64_t available_size = capacity_ - reservable_;
    if (available_size == 0) {
      return {nullptr, 0};
    }
    uint64_t reservation_size = std::min(size, available_size);
    void* reservation = &(base_[reservable_]);
    return {reservation, static_cast<size_t>(reservation_size)};
  }

  /**
   * Commit a Reservation that was previously obtained from a call to reserve().
   * The Reservation's size is added to the Data section.
   * @param reservation a reservation obtained from a previous call to reserve().
   *        If the reservation is not from this Slice, commit() will return false.
   *        If the caller is committing fewer bytes than provided by reserve(), it
   *        should change the len_ field of the reservation before calling commit().
   *        For example, if a caller reserve()s 4KB to do a nonblocking socket read,
   *        and the read only returns two bytes, the caller should set
   *        reservation.len_ = 2 and then call `commit(reservation)`.
   * @return whether the Reservation was successfully committed to the Slice.
   */
  bool commit(const Reservation& reservation) {
    if (static_cast<const uint8_t*>(reservation.mem_) != base_ + reservable_ ||
        reservable_ + reservation.len_ > capacity_ || reservable_ >= capacity_) {
      // The reservation is not from this Slice.
      return false;
    }
    reservable_ += reservation.len_;
    return true;
  }

  /**
   * Copy as much of the supplied data as possible to the end of the slice.
   * @param data start of the data to copy.
   * @param size number of bytes to copy.
   * @return number of bytes copied (may be a smaller than size, may even be zero).
   */
  uint64_t append(const void* data, uint64_t size) {
    uint64_t copy_size = std::min(size, reservableSize());
    if (copy_size == 0) {
      return 0;
    }
    uint8_t* dest = base_ + reservable_;
    reservable_ += copy_size;
    // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
    memcpy(dest, data, copy_size); // NOLINT(safe-memcpy)
    return copy_size;
  }

  /**
   * Copy as much of the supplied data as possible to the front of the slice.
   * If only part of the data will fit in the slice, the bytes from the _end_ are
   * copied.
   * @param data start of the data to copy.
   * @param size number of bytes to copy.
   * @return number of bytes copied (may be a smaller than size, may even be zero).
   */
  uint64_t prepend(const void* data, uint64_t size) {
    const uint8_t* src = static_cast<const uint8_t*>(data);
    uint64_t copy_size;
    if (dataSize() == 0) {
      // There is nothing in the slice, so put the data at the very end in case the caller
      // later tries to prepend anything else in front of it.
      copy_size = std::min(size, reservableSize());
      reservable_ = capacity_;
      data_ = capacity_ - copy_size;
    } else {
      if (data_ == 0) {
        // There is content in the slice, and no space in front of it to write anything.
        return 0;
      }
      // Write into the space in front of the slice's current content.
      copy_size = std::min(size, data_);
      data_ -= copy_size;
    }
    memcpy(base_ + data_, src + size - copy_size, copy_size); // NOLINT(safe-memcpy)
    return copy_size;
  }

  /**
   * Describe the in-memory representation of the slice. For use
   * in tests that want to make assertions about the specific arrangement of
   * bytes in a slice.
   */
  struct SliceRepresentation {
    uint64_t data;
    uint64_t reservable;
    uint64_t capacity;
  };
  SliceRepresentation describeSliceForTest() const {
    return SliceRepresentation{dataSize(), reservableSize(), capacity_};
  }

  /**
   * Move all drain trackers and charges from the current slice to the destination slice.
   */
  void transferDrainTrackersTo(Slice& destination) {
    destination.drain_trackers_.splice(destination.drain_trackers_.end(), drain_trackers_);
    ASSERT(drain_trackers_.empty());
  }

  /**
   * Add a drain tracker to the slice.
   */
  void addDrainTracker(std::function<void()> drain_tracker) {
    drain_trackers_.emplace_back(std::move(drain_tracker));
  }

  /**
   * Call all drain trackers associated with the slice, then clear
   * the drain tracker list.
   */
  void callAndClearDrainTrackersAndCharges() {
    for (const auto& drain_tracker : drain_trackers_) {
      drain_tracker();
    }
    drain_trackers_.clear();

    if (account_) {
      account_->credit(capacity_);
      account_.reset();
    }
  }

  /**
   * Charges the provided account for the resources if these conditions hold:
   * - we're not already charging for this slice
   * - the given account is non-null
   * - the slice owns backing memory
   */
  void maybeChargeAccount(const BufferMemoryAccountSharedPtr& account) {
    if (account_ != nullptr || storage_ == nullptr || account == nullptr) {
      return;
    }
    account->charge(capacity_);
    account_ = account;
  }

  static constexpr uint32_t default_slice_size_ = 16384;

  static FreeListReference freeList() { return FreeListReference(free_list_); }

protected:
  /**
   * Compute a slice size big enough to hold a specified amount of data.
   * @param data_size the minimum amount of data the slice must be able to store, in bytes.
   * @return a recommended slice size, in bytes.
   */
  static uint64_t sliceSize(uint64_t data_size) {
    static constexpr uint64_t PageSize = 4096;
    const uint64_t num_pages = (data_size + PageSize - 1) / PageSize;
    return num_pages * PageSize;
  }

  static StoragePtr newStorage(uint64_t capacity, absl::optional<FreeListReference> free_list_opt) {
    ASSERT(sliceSize(default_slice_size_) == default_slice_size_,
           "default_slice_size_ incompatible with sliceSize()");
    ASSERT(sliceSize(capacity) == capacity,
           "newStorage should only be called on values returned from sliceSize()");
    ASSERT(!free_list_opt.has_value() || &free_list_opt->free_list_ == &free_list_);

    StoragePtr storage;
    if (capacity == default_slice_size_ && free_list_opt.has_value()) {
      FreeListType& free_list = free_list_opt->free_list_;
      if (!free_list.empty()) {
        storage = std::move(free_list.back());
        ASSERT(storage != nullptr);
        ASSERT(free_list.back() == nullptr);
        free_list.pop_back();
        return storage;
      }
    }

    storage.reset(new uint8_t[capacity]);
    return storage;
  }

  static void freeStorage(StoragePtr storage, uint64_t capacity,
                          absl::optional<FreeListReference> free_list_opt = absl::nullopt) {
    if (storage == nullptr) {
      return;
    }

    if (capacity == default_slice_size_ && free_list_opt.has_value()) {
      FreeListType& free_list = free_list_opt->free_list_;
      if (free_list.size() < free_list_max_) {
        free_list.emplace_back(std::move(storage));
        ASSERT(storage == nullptr);
        return;
      }
    }

    storage.reset();
  }

  static thread_local FreeListType free_list_;

  /** Length of the byte array that base_ points to. This is also the offset in bytes from the start
   * of the slice to the end of the Reservable section. */
  uint64_t capacity_;

  /** Backing storage for mutable slices which own their own storage. This storage should never be
   * accessed directly; access base_ instead. */
  StoragePtr storage_;

  /** Start of the slice. Points to storage_ iff the slice owns its own storage. */
  uint8_t* base_{nullptr};

  /** Offset in bytes from the start of the slice to the start of the Data section. */
  uint64_t data_;

  /** Offset in bytes from the start of the slice to the start of the Reservable section which is
   * also the end of the Data section. */
  uint64_t reservable_;

  /** Hooks to execute when the slice is destroyed. */
  std::list<std::function<void()>> drain_trackers_;

  /** Account associated with this slice. This may be null. When
   * coalescing with another slice, we do not transfer over their account. */
  BufferMemoryAccountSharedPtr account_;
};

class OwnedImpl;

class SliceDataImpl : public SliceData {
public:
  explicit SliceDataImpl(Slice&& slice) : slice_(std::move(slice)) {}

  // SliceData
  absl::Span<uint8_t> getMutableData() override {
    RELEASE_ASSERT(slice_.isMutable(), "Not allowed to call getMutableData if slice is immutable");
    return {slice_.data(), static_cast<absl::Span<uint8_t>::size_type>(slice_.dataSize())};
  }

private:
  friend OwnedImpl;
  Slice slice_;
};

/**
 * Queue of Slice that supports efficient read and write access to both
 * the front and the back of the queue.
 * @note This class has similar properties to std::deque<T>. The reason for using
 *       a custom deque implementation is that benchmark testing during development
 *       revealed that std::deque was too slow to reach performance parity with the
 *       prior evbuffer-based buffer implementation.
 */
class SliceDeque {
public:
  SliceDeque() : ring_(inline_ring_), capacity_(InlineRingCapacity) {}

  SliceDeque(SliceDeque&& rhs) noexcept {
    // This custom move constructor is needed so that ring_ will be updated properly.
    std::move(rhs.inline_ring_, rhs.inline_ring_ + InlineRingCapacity, inline_ring_);
    external_ring_ = std::move(rhs.external_ring_);
    ring_ = (external_ring_ != nullptr) ? external_ring_.get() : inline_ring_;
    start_ = rhs.start_;
    size_ = rhs.size_;
    capacity_ = rhs.capacity_;
  }

  SliceDeque& operator=(SliceDeque&& rhs) noexcept {
    // This custom assignment move operator is needed so that ring_ will be updated properly.
    std::move(rhs.inline_ring_, rhs.inline_ring_ + InlineRingCapacity, inline_ring_);
    external_ring_ = std::move(rhs.external_ring_);
    ring_ = (external_ring_ != nullptr) ? external_ring_.get() : inline_ring_;
    start_ = rhs.start_;
    size_ = rhs.size_;
    capacity_ = rhs.capacity_;
    return *this;
  }

  void emplace_back(Slice&& slice) { // NOLINT(readability-identifier-naming)
    growRing();
    size_t index = internalIndex(size_);
    ring_[index] = std::move(slice);
    size_++;
  }

  void emplace_front(Slice&& slice) { // NOLINT(readability-identifier-naming)
    growRing();
    start_ = (start_ == 0) ? capacity_ - 1 : start_ - 1;
    ring_[start_] = std::move(slice);
    size_++;
  }

  bool empty() const { return size() == 0; }
  size_t size() const { return size_; }

  Slice& front() { return ring_[start_]; }
  const Slice& front() const { return ring_[start_]; }
  Slice& back() { return ring_[internalIndex(size_ - 1)]; }
  const Slice& back() const { return ring_[internalIndex(size_ - 1)]; }

  Slice& operator[](size_t i) {
    ASSERT(!empty());
    return ring_[internalIndex(i)];
  }
  const Slice& operator[](size_t i) const {
    ASSERT(!empty());
    return ring_[internalIndex(i)];
  }

  void pop_front() { // NOLINT(readability-identifier-naming)
    if (size() == 0) {
      return;
    }
    front() = Slice();
    size_--;
    start_++;
    if (start_ == capacity_) {
      start_ = 0;
    }
  }

  void pop_back() { // NOLINT(readability-identifier-naming)
    if (size() == 0) {
      return;
    }
    back() = Slice();
    size_--;
  }

  /**
   * Forward const iterator for SliceDeque.
   * @note this implementation currently supports the minimum functionality needed to support
   *       the `for (const auto& slice : slice_deque)` idiom.
   */
  class ConstIterator {
  public:
    const Slice& operator*() { return deque_[index_]; }

    ConstIterator operator++() {
      index_++;
      return *this;
    }

    bool operator!=(const ConstIterator& rhs) const {
      return &deque_ != &rhs.deque_ || index_ != rhs.index_;
    }

    friend class SliceDeque;

  private:
    ConstIterator(const SliceDeque& deque, size_t index) : deque_(deque), index_(index) {}
    const SliceDeque& deque_;
    size_t index_;
  };

  ConstIterator begin() const noexcept { return {*this, 0}; }

  ConstIterator end() const noexcept { return {*this, size_}; }

private:
  constexpr static size_t InlineRingCapacity = 8;

  size_t internalIndex(size_t index) const {
    size_t internal_index = start_ + index;
    if (internal_index >= capacity_) {
      internal_index -= capacity_;
      ASSERT(internal_index < capacity_);
    }
    return internal_index;
  }

  void growRing() {
    if (size_ < capacity_) {
      return;
    }
    const size_t new_capacity = capacity_ * 2;
    auto new_ring = std::make_unique<Slice[]>(new_capacity);
    size_t src = start_;
    size_t dst = 0;
    for (size_t i = 0; i < size_; i++) {
      new_ring[dst++] = std::move(ring_[src++]);
      if (src == capacity_) {
        src = 0;
      }
    }
    external_ring_.swap(new_ring);
    ring_ = external_ring_.get();
    start_ = 0;
    capacity_ = new_capacity;
  }

  Slice inline_ring_[InlineRingCapacity];
  std::unique_ptr<Slice[]> external_ring_;
  Slice* ring_; // points to start of either inline or external ring.
  size_t start_{0};
  size_t size_{0};
  size_t capacity_;
};

/**
 * An implementation of BufferFragment where a releasor callback is called when the data is
 * no longer needed.
 */
class BufferFragmentImpl : NonCopyable, public BufferFragment {
public:
  /**
   * Creates a new wrapper around the externally owned <data> of size <size>.
   * The caller must ensure <data> is valid until releasor() is called, or for the lifetime of the
   * fragment. releasor() is called with <data>, <size> and <this> to allow caller to delete
   * the fragment object.
   * @param data external data to reference
   * @param size size of data
   * @param releasor a callback function to be called when data is no longer needed.
   */
  BufferFragmentImpl(
      const void* data, size_t size,
      const std::function<void(const void*, size_t, const BufferFragmentImpl*)>& releasor)
      : data_(data), size_(size), releasor_(releasor) {}

  // Buffer::BufferFragment
  const void* data() const override { return data_; }
  size_t size() const override { return size_; }
  void done() override {
    if (releasor_) {
      releasor_(data_, size_, this);
    }
  }

private:
  const void* const data_;
  const size_t size_;
  const std::function<void(const void*, size_t, const BufferFragmentImpl*)> releasor_;
};

class LibEventInstance : public Instance {
public:
  // Called after accessing the memory in buffer() directly to allow any post-processing.
  virtual void postProcess() PURE;
};

/**
 * Wrapper for uint64_t that asserts upon integer overflow and underflow.
 */
class OverflowDetectingUInt64 {
public:
  operator uint64_t() const { return value_; }

  OverflowDetectingUInt64& operator+=(uint64_t size) {
    uint64_t new_value = value_ + size;
    RELEASE_ASSERT(new_value >= value_, "64-bit unsigned integer overflowed");
    value_ = new_value;
    return *this;
  }

  OverflowDetectingUInt64& operator-=(uint64_t size) {
    RELEASE_ASSERT(value_ >= size, "unsigned integer underflowed");
    value_ -= size;
    return *this;
  }

private:
  uint64_t value_{0};
};

/**
 * Wraps an allocated and owned buffer.
 *
 * Note that due to the internals of move(), OwnedImpl is not
 * compatible with non-OwnedImpl buffers.
 */
class OwnedImpl : public LibEventInstance {
public:
  OwnedImpl();
  OwnedImpl(absl::string_view data);
  OwnedImpl(const Instance& data);
  OwnedImpl(const void* data, uint64_t size);
  OwnedImpl(BufferMemoryAccountSharedPtr account);

  // Buffer::Instance
  void addDrainTracker(std::function<void()> drain_tracker) override;
  void bindAccount(BufferMemoryAccountSharedPtr account) override;
  void add(const void* data, uint64_t size) override;
  void addBufferFragment(BufferFragment& fragment) override;
  void add(absl::string_view data) override;
  void add(const Instance& data) override;
  void prepend(absl::string_view data) override;
  void prepend(Instance& data) override;
  void copyOut(size_t start, uint64_t size, void* data) const override;
  void drain(uint64_t size) override;
  RawSliceVector getRawSlices(absl::optional<uint64_t> max_slices = absl::nullopt) const override;
  RawSlice frontSlice() const override;
  SliceDataPtr extractMutableFrontSlice() override;
  uint64_t length() const override;
  void* linearize(uint32_t size) override;
  void move(Instance& rhs) override;
  void move(Instance& rhs, uint64_t length) override;
  Reservation reserveForRead() override;
  ReservationSingleSlice reserveSingleSlice(uint64_t length, bool separate_slice = false) override;
  ssize_t search(const void* data, uint64_t size, size_t start, size_t length) const override;
  bool startsWith(absl::string_view data) const override;
  std::string toString() const override;

  // LibEventInstance
  void postProcess() override;

  /**
   * Create a new slice at the end of the buffer, and copy the supplied content into it.
   * @param data start of the content to copy.
   *
   */
  virtual void appendSliceForTest(const void* data, uint64_t size);

  /**
   * Create a new slice at the end of the buffer, and copy the supplied string into it.
   * @param data the string to append to the buffer.
   */
  virtual void appendSliceForTest(absl::string_view data);

  /**
   * @return the BufferMemoryAccount bound to this buffer, if any.
   */
  BufferMemoryAccountSharedPtr getAccountForTest();

  // Does not implement watermarking.
  // TODO(antoniovicente) Implement watermarks by merging the OwnedImpl and WatermarkBuffer
  // implementations. Also, make high-watermark config a constructor argument.
  void setWatermarks(uint32_t) override { ASSERT(false, "watermarks not implemented."); }
  uint32_t highWatermark() const override { return 0; }
  bool highWatermarkTriggered() const override { return false; }

  /**
   * Describe the in-memory representation of the slices in the buffer. For use
   * in tests that want to make assertions about the specific arrangement of
   * bytes in the buffer.
   */
  std::vector<Slice::SliceRepresentation> describeSlicesForTest() const;

  /**
   * Create a reservation for reading with a non-default length. Used in benchmark tests.
   */
  Reservation reserveForReadWithLengthForTest(uint64_t length) {
    return reserveWithMaxLength(length);
  }

protected:
  static constexpr uint64_t default_read_reservation_size_ =
      Reservation::MAX_SLICES_ * Slice::default_slice_size_;

  /**
   * Create a reservation with a maximum length.
   */
  Reservation reserveWithMaxLength(uint64_t max_length);

  void commit(uint64_t length, absl::Span<RawSlice> slices,
              ReservationSlicesOwnerPtr slices_owner) override;

private:
  /**
   * @param rhs another buffer
   * @return whether the rhs buffer is also an instance of OwnedImpl (or a subclass) that
   *         uses the same internal implementation as this buffer.
   */
  bool isSameBufferImpl(const Instance& rhs) const;

  void addImpl(const void* data, uint64_t size);
  void drainImpl(uint64_t size);

  /**
   * Moves contents of the `other_slice` by either taking its ownership or coalescing it
   * into an existing slice.
   * NOTE: the caller is responsible for draining the buffer that contains the `other_slice`.
   */
  void coalesceOrAddSlice(Slice&& other_slice);

  /** Ring buffer of slices. */
  SliceDeque slices_;

  /** Sum of the dataSize of all slices. */
  OverflowDetectingUInt64 length_;

  BufferMemoryAccountSharedPtr account_;

  struct OwnedImplReservationSlicesOwner : public ReservationSlicesOwner {
    virtual absl::Span<Slice> ownedSlices() PURE;
  };

  struct OwnedImplReservationSlicesOwnerMultiple : public OwnedImplReservationSlicesOwner {
    // Optimization: get the thread_local freeList() once per Reservation, outside the loop.
    OwnedImplReservationSlicesOwnerMultiple() : free_list_(Slice::freeList()) {}

    ~OwnedImplReservationSlicesOwnerMultiple() override {
      while (!owned_slices_.empty()) {
        owned_slices_.back().freeStorage(free_list_);
        owned_slices_.pop_back();
      }
    }
    absl::Span<Slice> ownedSlices() override { return absl::MakeSpan(owned_slices_); }

    Slice::FreeListReference free_list_;
    absl::InlinedVector<Slice, Buffer::Reservation::MAX_SLICES_> owned_slices_;
  };

  struct OwnedImplReservationSlicesOwnerSingle : public OwnedImplReservationSlicesOwner {
    absl::Span<Slice> ownedSlices() override { return absl::MakeSpan(&owned_slice_, 1); }

    Slice owned_slice_;
  };
};

using BufferFragmentPtr = std::unique_ptr<BufferFragment>;

/**
 * An implementation of BufferFragment where a releasor callback is called when the data is
 * no longer needed. Copies data into internal buffer.
 */
class OwnedBufferFragmentImpl final : public BufferFragment, public InlineStorage {
public:
  using Releasor = std::function<void(const OwnedBufferFragmentImpl*)>;

  /**
   * Copies the data into internal buffer. The releasor is called when the data has been
   * fully drained or the buffer that contains this fragment is destroyed.
   * @param data external data to reference
   * @param releasor a callback function to be called when data is no longer needed.
   */

  static BufferFragmentPtr create(absl::string_view data, const Releasor& releasor) {
    return BufferFragmentPtr(new (sizeof(OwnedBufferFragmentImpl) + data.size())
                                 OwnedBufferFragmentImpl(data, releasor));
  }

  // Buffer::BufferFragment
  const void* data() const override { return data_; }
  size_t size() const override { return size_; }
  void done() override { releasor_(this); }

private:
  OwnedBufferFragmentImpl(absl::string_view data, const Releasor& releasor)
      : releasor_(releasor), size_(data.size()) {
    ASSERT(releasor != nullptr);
    memcpy(data_, data.data(), data.size()); // NOLINT(safe-memcpy)
  }

  const Releasor releasor_;
  const size_t size_;
  uint8_t data_[];
};

using OwnedBufferFragmentImplPtr = std::unique_ptr<OwnedBufferFragmentImpl>;

/**
 * A BufferMemoryAccountImpl tracks allocated bytes across associated buffers and
 * slices that originate from those buffers, or are untagged and pass through an
 * associated buffer.
 */
class BufferMemoryAccountImpl : public BufferMemoryAccount {
public:
  BufferMemoryAccountImpl() = default;
  ~BufferMemoryAccountImpl() override { ASSERT(buffer_memory_allocated_ == 0); }

  // Make not copyable
  BufferMemoryAccountImpl(const BufferMemoryAccountImpl&) = delete;
  BufferMemoryAccountImpl& operator=(const BufferMemoryAccountImpl&) = delete;

  // Make not movable.
  BufferMemoryAccountImpl(BufferMemoryAccountImpl&&) = delete;
  BufferMemoryAccountImpl& operator=(BufferMemoryAccountImpl&&) = delete;

  uint64_t balance() const { return buffer_memory_allocated_; }
  void charge(uint64_t amount) override {
    // Check overflow
    ASSERT(std::numeric_limits<uint64_t>::max() - buffer_memory_allocated_ >= amount);
    buffer_memory_allocated_ += amount;
  }

  void credit(uint64_t amount) override {
    ASSERT(buffer_memory_allocated_ >= amount);
    buffer_memory_allocated_ -= amount;
  }

private:
  uint64_t buffer_memory_allocated_ = 0;
};

} // namespace Buffer
} // namespace Envoy
