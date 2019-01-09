#pragma once

#include <cstdint>
#include <deque>
#include <string>

#include "envoy/buffer/buffer.h"

#include "common/common/assert.h"
#include "common/common/non_copyable.h"

namespace Envoy {
namespace Buffer {

/**
 * A Slice manages a contiguous block of bytes.
 * The block is arranged like this:
 *                   |<- data_size() -->|<- reservable_size() ->|
 * +-----------------+------------------+-----------------------+
 * | Drained         | Data             | Reservable            |
 * | Unused space    | Usable content   | New content can be    |
 * | that formerly   |                  | added here with       |
 * | was in the Data |                  | reserve()/commit()    |
 * | section         |                  |                       |
 * +-----------------+------------------+-----------------------+
 *                   ^
 *                   |
 *                   data()
 */
class Slice {
public:
  using Reservation = RawSlice;

  virtual ~Slice() = default;

  /**
   * @return a pointer to the start of the usable content.
   */
  const void* data() const { return base_ + data_; }

  /**
   * @return a pointer to the start of the usable content.
   */
  void* data() { return base_ + data_; }

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
    if (data_ == reservable_ && !reservation_outstanding_) {
      // There is no more content in the slice, and there is no outstanding reservation,
      // so reset the Data section to the start of the slice to facilitate reuse.
      data_ = reservable_ = 0;
    }
  }

  /**
   * @return the number of bytes available to be reserve()d.
   * @note If reserve() has been called without a corresponding commit(), this method
   *       should return 0.
   * @note Read-only implementations of Slice should return zero from this method.
   */
  uint64_t reservableSize() const {
    if (reservation_outstanding_) {
      return 0;
    }
    return size_ - reservable_;
  }

  /**
   * Reserve `size` bytes that the caller can populate with content. The caller SHOULD then
   * call commit() to add the newly populated content from the Reserved section to the Data
   * section.
   * @note If there is already an oustanding reservation (i.e., a reservation obtained
   *       from reserve() that has not been released by calling commit()), this method will
   *       return {nullptr, 0}.
   * @param size the number of bytes to reserve. The Slice implementation MAY reserve
   *        fewer bytes than requested (for example, if it doesn't have enough room in the
   *        Reservable section to fulfill the whole request).
   * @return a tuple containing the address of the start of resulting reservation and the
   *         reservation size in bytes. If the address is null, the reservation failed.
   * @note Read-only implementations of Slice should return {nullptr, 0} from this method.
   */
  Reservation reserve(uint64_t size) {
    if (reservation_outstanding_) {
      return {nullptr, 0};
    }
    uint64_t available_size = size_ - reservable_;
    if (available_size == 0) {
      return {nullptr, 0};
    }
    uint64_t reservation_size = std::min(size, available_size);
    void* reservation = &(base_[reservable_]);
    reservation_outstanding_ = true;
    return {reservation, reservation_size};
  }

  /**
   * Commit a Reservation that was previously obtained from a call to reserve().
   * The Reservation's size is added to the Data section.
   * @param reservation a reservation obtained from a previous call to reserve().
   *        If the reservation is not from this Slice, commit() will return false.
   *        If the caller is committing fewer bytes than provided by reserve(), it
   *        should change the mem_ field of the reservation before calling commit().
   *        For example, if a caller reserve()s 4KB to do a nonblocking socket read,
   *        and the read only returns two bytes, the caller should set
   *        reservation.mem_ = 2 and then call `commit(reservation)`.
   * @return whether the Reservation was successfully committed to the Slice.
   */
  bool commit(const Reservation& reservation) {
    if (static_cast<const uint8_t*>(reservation.mem_) != base_ + reservable_ ||
        reservable_ + reservation.len_ > size_ || reservable_ >= size_) {
      // The reservation is not from this OwnedSlice.
      return false;
    }
    ASSERT(reservation_outstanding_);
    reservable_ += reservation.len_;
    reservation_outstanding_ = false;
    return true;
  }

protected:
  Slice(uint64_t data, uint64_t reservable, uint64_t size) : data_(data), reservable_(reservable), size_(size) {}

  /** Start of the slice - subclasses must set this */
  uint8_t* base_{nullptr};

  /** Offset in bytes from the start of the slice to the start of the Data section */
  uint64_t data_;

  /** Offset in bytes from the start of the slice to the start of the Reservable section */
  uint64_t reservable_;

  /** Total number of bytes in the slice */
  uint64_t size_;

  /** Whether reserve() has been called without a corresponding commit(). */
  bool reservation_outstanding_{false};
};

using SlicePtr = std::unique_ptr<Slice>;

class OwnedSlice : public Slice {
public:
  static SlicePtr create(uint64_t size) {
    uint64_t slice_size = sliceSize(size);
    return SlicePtr(new (slice_size) OwnedSlice(slice_size));
  }

  static SlicePtr create(const void* data, uint64_t size) {
    uint64_t slice_size = sliceSize(size);
    OwnedSlice* slice = new (slice_size) OwnedSlice(slice_size);
    memcpy(slice->base_, data, size);
    slice->reservable_ = size;
    return SlicePtr(slice);
  }

private:
  void* operator new(size_t object_size, size_t data_size) {
   return ::operator new(object_size + data_size);
  }

  OwnedSlice(uint64_t size) : Slice(0, 0, sliceSize(size)) {
    base_ = storage_;
  }

  OwnedSlice(const void* data, uint64_t size) : OwnedSlice(size) {
    memcpy(base_, data, size);
    reservable_ = size;
  }

  /**
   * Compute a slice size big enough to hold a specified amount of data.
   * @param data_size the minimum amount of data the slice must be able to store, in bytes.
   * @return a recommended slice size, in bytes.
   */
  static uint64_t sliceSize(uint64_t data_size) {
    uint64_t slice_size = 32;
    while (slice_size < data_size) {
      slice_size <<= 1;
      if (slice_size == 0) {
        // Integer overflow
        return data_size;
      }
    }
    return slice_size;
  }

  uint8_t storage_[];
};

class UnownedSlice : public Slice {
public:
  UnownedSlice(BufferFragment& fragment) : Slice(0, fragment.size(), fragment.size()), fragment_(fragment) {
    base_ = static_cast<uint8_t*>(const_cast<void*>(fragment.data()));
  }

  ~UnownedSlice() override { fragment_.done(); }

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

/**
 * Wraps an allocated and owned evbuffer.
 *
 * Note that due to the internals of move(), OwnedImpl is not
 * compatible with non-OwnedImpl buffers.
 */
class OwnedImpl : public Instance {
public:
  OwnedImpl();
  OwnedImpl(absl::string_view data);
  OwnedImpl(const Instance& data);
  OwnedImpl(const void* data, uint64_t size);

  // Buffer::Instance
  void add(const void* data, uint64_t size) override;
  void addBufferFragment(BufferFragment& fragment) override;
  void add(absl::string_view data) override;
  void add(const Instance& data) override;
  void prepend(absl::string_view data) override;
  void prepend(Instance& data) override;
  void commit(RawSlice* iovecs, uint64_t num_iovecs) override;
  void copyOut(size_t start, uint64_t size, void* data) const override;
  void drain(uint64_t size) override;
  uint64_t getRawSlices(RawSlice* out, uint64_t out_size) const override;
  uint64_t length() const override;
  void* linearize(uint32_t size) override;
  void move(Instance& rhs) override;
  void move(Instance& rhs, uint64_t length) override;
  Api::SysCallIntResult read(int fd, uint64_t max_length) override;
  uint64_t reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) override;
  ssize_t search(const void* data, uint64_t size, size_t start) const override;
  Api::SysCallIntResult write(int fd) override;
  std::string toString() const override;

protected:
  // Copy data to the end of the buffer.
  void append(const void* data, uint64_t size);

  // Called after accessing the memory in buffer() directly to allow any post-processing.
  virtual void postProcess();

private:
  std::deque<SlicePtr> slices_;

  /** Sum of the dataSize of all slices. */
  uint64_t length_{0};
};

} // namespace Buffer
} // namespace Envoy
