#pragma once

#include <cstdint>
#include <deque>
#include <string>

#include "envoy/buffer/buffer.h"

#include "common/common/non_copyable.h"

namespace Envoy {
namespace Buffer {

/**
 * A BufferSlice manages a contiguous block of bytes.
 * The block is arranged like this:
 *                   |<- data_size() -->|                    |<- reservable_size() ->|
 * +-----------------+------------------+--------------------+-----------------------+
 * | Drained         | Data             | Reserved           | Reservable            |
 * | Unused space    | Usable content   | Additional content | Unused space that can |
 * | that formerly   |                  | can be added here  | can be moved into     |
 * | was in the Data |                  |                    | the Reserved section  |
 * | section         |                  |                    |                       |
 * +-----------------+------------------+--------------------+-----------------------+
 *                   ^
 *                   |
 *                   data()
 */
class BufferSlice {
public:
  using Reservation = RawSlice;

  virtual ~BufferSlice() = default;

  /**
   * @return a pointer to the start of the usable content.
   */
  virtual const void* data() const PURE;

  /**
   * @return a pointer to the start of the usable content.
   */
  virtual void* data() PURE;

  /**
   * @return the size in bytes of the usable content.
   */
  virtual uint64_t dataSize() const PURE;

  /**
   * Remove the first `size` bytes of usable content. Runs in O(1) time.
   * @param size number of bytes to remove. If greater than data_size(), the result is undefined.
   */
  virtual void drain(uint64_t size) PURE;

  /**
   * @return the number of bytes available to be reserve()d.
   * @note Read-only implementations of BufferSlice should return zero from this method.
   */
  virtual uint64_t reservableSize() const PURE;

  /**
   * Reserve `size` bytes that the caller can populate with content. The caller SHOULD then
   * call commit() to add the newly populated content from the Reserved section to the Data
   * section.
   * @note It is valid to call reserve() multiple times without a commit in between. The
   *       result will be a succession of contiguous, reserved spans of memory, where the
   *       first span starts right after the Data section. These spans MUST be commit()ed
   *       in the order in which they were reserved; the result of calling commit() out
   *       of order is undefined.
   * @param size the number of bytes to reserve. The BufferSlice implementation MAY reserve
   *        fewer bytes than requested (for example, if it doesn't have enough room in the
   *        Reservable section to fulfill the whole request).
   * @return a tuple containing the address of the start of resulting reservation and the
   *         reservation size in bytes. If the address is null, the reservation failed.
   * @note Read-only implementations of BufferSlice should return {nullptr, 0} from this method.
   */
  virtual Reservation reserve(uint64_t size) PURE;

  /**
   * Commit a Reservation that was previously obtained from a call to reserve().
   * The Reservation's size is added to the Data section.
   * @param reservation a reservation obtained from a previous call to reserve().
   *        If the reservation is not from this BufferSlice, commit() will return false.
   * @param size the number of bytes at the start of the reservation to commit. This number
   *             MAY be smaller than the reservation size. For example, if a caller reserve()s
   *             4KB to do a nonblocking socket read, and the read only returns two bytes, the
   *             caller should then invoke `commit(reservation, 2)`.
   * @return whether the Reservation was successfully committed to the BufferSlice.
   */
  virtual bool commit(const Reservation& reservation, uint64_t size) PURE;
};

using BufferSlicePtr = std::unique_ptr<BufferSlice>;

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
  std::deque<BufferSlicePtr> slices_;
};

} // namespace Buffer
} // namespace Envoy
