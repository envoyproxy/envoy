#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
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
 * | section         |                |                      |
 * +-----------------+----------------+----------------------+
 *                   ^
 *                   |
 *                   data()
 */
class Slice : public SliceData {
public:
  using Reservation = RawSlice;

  ~Slice() override { callAndClearDrainTrackers(); }

  // SliceData
  absl::Span<uint8_t> getMutableData() override {
    RELEASE_ASSERT(isMutable(), "Not allowed to call getMutableData if slice is immutable");
    return {base_ + data_, static_cast<absl::Span<uint8_t>::size_type>(reservable_ - data_)};
  }

  /**
   * @return true if the data in the slice is mutable
   */
  virtual bool isMutable() const { return false; }

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
      // The reservation is not from this OwnedSlice.
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
    memcpy(dest, data, copy_size);
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
    memcpy(base_ + data_, src + size - copy_size, copy_size);
    return copy_size;
  }

  /**
   * @return true if content in this Slice can be coalesced into another Slice.
   */
  virtual bool canCoalesce() const { return true; }

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
   * Move all drain trackers from the current slice to the destination slice.
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
  void callAndClearDrainTrackers() {
    for (const auto& drain_tracker : drain_trackers_) {
      drain_tracker();
    }
    drain_trackers_.clear();
  }

protected:
  Slice(uint64_t data, uint64_t reservable, uint64_t capacity)
      : data_(data), reservable_(reservable), capacity_(capacity) {}

  /** Start of the slice - subclasses must set this */
  uint8_t* base_{nullptr};

  /** Offset in bytes from the start of the slice to the start of the Data section */
  uint64_t data_;

  /** Offset in bytes from the start of the slice to the start of the Reservable section */
  uint64_t reservable_;

  /** Total number of bytes in the slice */
  uint64_t capacity_;

  std::list<std::function<void()>> drain_trackers_;
};

using SlicePtr = std::unique_ptr<Slice>;

// OwnedSlice can not be derived from as it has variable sized array as member.
class OwnedSlice final : public Slice, public InlineStorage {
public:
  /**
   * Create an empty OwnedSlice.
   * @param capacity number of bytes of space the slice should have.
   * @return an OwnedSlice with at least the specified capacity.
   */
  static SlicePtr create(uint64_t capacity) {
    uint64_t slice_capacity = sliceSize(capacity);
    return SlicePtr(new (slice_capacity) OwnedSlice(slice_capacity));
  }

  /**
   * Create an OwnedSlice and initialize it with a copy of the supplied copy.
   * @param data the content to copy into the slice.
   * @param size length of the content.
   * @return an OwnedSlice containing a copy of the content, which may (dependent on
   *         the internal implementation) have a nonzero amount of reservable space at the end.
   */
  static SlicePtr create(const void* data, uint64_t size) {
    uint64_t slice_capacity = sliceSize(size);
    std::unique_ptr<OwnedSlice> slice(new (slice_capacity) OwnedSlice(slice_capacity));
    memcpy(slice->base_, data, size);
    slice->reservable_ = size;
    return slice;
  }

private:
  OwnedSlice(uint64_t size) : Slice(0, 0, size) { base_ = storage_; }

  bool isMutable() const override { return true; }

  /**
   * Compute a slice size big enough to hold a specified amount of data.
   * @param data_size the minimum amount of data the slice must be able to store, in bytes.
   * @return a recommended slice size, in bytes.
   */
  static uint64_t sliceSize(uint64_t data_size) {
    static constexpr uint64_t PageSize = 4096;
    const uint64_t num_pages = (sizeof(OwnedSlice) + data_size + PageSize - 1) / PageSize;
    return num_pages * PageSize - sizeof(OwnedSlice);
  }

  uint8_t storage_[];
};

/**
 * Queue of SlicePtr that supports efficient read and write access to both
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

  void emplace_back(SlicePtr&& slice) {
    growRing();
    size_t index = internalIndex(size_);
    ring_[index] = std::move(slice);
    size_++;
  }

  void emplace_front(SlicePtr&& slice) {
    growRing();
    start_ = (start_ == 0) ? capacity_ - 1 : start_ - 1;
    ring_[start_] = std::move(slice);
    size_++;
  }

  bool empty() const { return size() == 0; }
  size_t size() const { return size_; }

  SlicePtr& front() { return ring_[start_]; }
  const SlicePtr& front() const { return ring_[start_]; }
  SlicePtr& back() { return ring_[internalIndex(size_ - 1)]; }
  const SlicePtr& back() const { return ring_[internalIndex(size_ - 1)]; }

  SlicePtr& operator[](size_t i) { return ring_[internalIndex(i)]; }
  const SlicePtr& operator[](size_t i) const { return ring_[internalIndex(i)]; }

  void pop_front() {
    if (size() == 0) {
      return;
    }
    front() = SlicePtr();
    size_--;
    start_++;
    if (start_ == capacity_) {
      start_ = 0;
    }
  }

  void pop_back() {
    if (size() == 0) {
      return;
    }
    back() = SlicePtr();
    size_--;
  }

  /**
   * Forward const iterator for SliceDeque.
   * @note this implementation currently supports the minimum functionality needed to support
   *       the `for (const auto& slice : slice_deque)` idiom.
   */
  class ConstIterator {
  public:
    const SlicePtr& operator*() { return deque_[index_]; }

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
    auto new_ring = std::make_unique<SlicePtr[]>(new_capacity);
    for (size_t i = 0; i < new_capacity; i++) {
      ASSERT(new_ring[i] == nullptr);
    }
    size_t src = start_;
    size_t dst = 0;
    for (size_t i = 0; i < size_; i++) {
      new_ring[dst++] = std::move(ring_[src++]);
      if (src == capacity_) {
        src = 0;
      }
    }
    for (size_t i = 0; i < capacity_; i++) {
      ASSERT(ring_[i].get() == nullptr);
    }
    external_ring_.swap(new_ring);
    ring_ = external_ring_.get();
    start_ = 0;
    capacity_ = new_capacity;
  }

  SlicePtr inline_ring_[InlineRingCapacity];
  std::unique_ptr<SlicePtr[]> external_ring_;
  SlicePtr* ring_; // points to start of either inline or external ring.
  size_t start_{0};
  size_t size_{0};
  size_t capacity_;
};

class UnownedSlice : public Slice {
public:
  UnownedSlice(BufferFragment& fragment)
      : Slice(0, fragment.size(), fragment.size()), fragment_(fragment) {
    base_ = static_cast<uint8_t*>(const_cast<void*>(fragment.data()));
  }

  ~UnownedSlice() override { fragment_.done(); }

  /**
   * BufferFragment objects encapsulated by UnownedSlice are used to track when response content
   * is written into transport connection. As a result these slices can not be coalesced when moved
   * between buffers.
   */
  bool canCoalesce() const override { return false; }

private:
  BufferFragment& fragment_;
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

  // Buffer::Instance
  void addDrainTracker(std::function<void()> drain_tracker) override;
  void add(const void* data, uint64_t size) override;
  void addBufferFragment(BufferFragment& fragment) override;
  void add(absl::string_view data) override;
  void add(const Instance& data) override;
  void prepend(absl::string_view data) override;
  void prepend(Instance& data) override;
  void commit(RawSlice* iovecs, uint64_t num_iovecs) override;
  void copyOut(size_t start, uint64_t size, void* data) const override;
  void drain(uint64_t size) override;
  RawSliceVector getRawSlices(absl::optional<uint64_t> max_slices = absl::nullopt) const override;
  SliceDataPtr extractMutableFrontSlice() override;
  uint64_t length() const override;
  void* linearize(uint32_t size) override;
  void move(Instance& rhs) override;
  void move(Instance& rhs, uint64_t length) override;
  uint64_t reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) override;
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
   * Describe the in-memory representation of the slices in the buffer. For use
   * in tests that want to make assertions about the specific arrangement of
   * bytes in the buffer.
   */
  std::vector<OwnedSlice::SliceRepresentation> describeSlicesForTest() const;

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
  void coalesceOrAddSlice(SlicePtr&& other_slice);

  /** Ring buffer of slices. */
  SliceDeque slices_;

  /** Sum of the dataSize of all slices. */
  OverflowDetectingUInt64 length_;
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
    memcpy(data_, data.data(), data.size());
  }

  const Releasor releasor_;
  const size_t size_;
  uint8_t data_[];
};

using OwnedBufferFragmentImplPtr = std::unique_ptr<OwnedBufferFragmentImpl>;

} // namespace Buffer
} // namespace Envoy
