#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/api/os_sys_calls.h"
#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/common/pure.h"

#include "common/common/assert.h"
#include "common/common/byte_order.h"
#include "common/common/utility.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Buffer {

/**
 * A raw memory data slice including location and length.
 */
struct RawSlice {
  void* mem_ = nullptr;
  size_t len_ = 0;

  bool operator==(const RawSlice& rhs) const { return mem_ == rhs.mem_ && len_ == rhs.len_; }
  bool operator!=(const RawSlice& rhs) const { return !(*this == rhs); }
};

using RawSliceVector = absl::InlinedVector<RawSlice, 16>;

/**
 * A wrapper class to facilitate passing in externally owned data to a buffer via addBufferFragment.
 * When the buffer no longer needs the data passed in through a fragment, it calls done() on it.
 */
class BufferFragment {
public:
  virtual ~BufferFragment() = default;
  /**
   * @return const void* a pointer to the referenced data.
   */
  virtual const void* data() const PURE;

  /**
   * @return size_t the size of the referenced data.
   */
  virtual size_t size() const PURE;

  /**
   * Called by a buffer when the referenced data is no longer needed.
   */
  virtual void done() PURE;
};

/**
 * A class to facilitate extracting buffer slices from a buffer instance.
 */
class SliceData {
public:
  virtual ~SliceData() = default;

  /**
   * @return a mutable view of the slice data.
   */
  virtual absl::Span<uint8_t> getMutableData() PURE;
};

using SliceDataPtr = std::unique_ptr<SliceData>;

class Reservation;
class ReservationSingleSlice;

// Base class for an object to manage the ownership for slices in a `Reservation` or
// `ReservationSingleSlice`.
class ReservationSlicesOwner {
public:
  virtual ~ReservationSlicesOwner() = default;
};

using ReservationSlicesOwnerPtr = std::unique_ptr<ReservationSlicesOwner>;

/**
 * A basic buffer abstraction.
 */
class Instance {
public:
  virtual ~Instance() = default;

  /**
   * Register function to call when the last byte in the last slice of this
   * buffer has fully drained. Note that slices may be transferred to
   * downstream buffers, drain trackers are transferred along with the bytes
   * they track so the function is called only after the last byte is drained
   * from all buffers.
   */
  virtual void addDrainTracker(std::function<void()> drain_tracker) PURE;

  /**
   * Copy data into the buffer (deprecated, use absl::string_view variant
   * instead).
   * TODO(htuch): Cleanup deprecated call sites.
   * @param data supplies the data address.
   * @param size supplies the data size.
   */
  virtual void add(const void* data, uint64_t size) PURE;

  /**
   * Add externally owned data into the buffer. No copying is done. fragment is not owned. When
   * the fragment->data() is no longer needed, fragment->done() is called.
   * @param fragment the externally owned data to add to the buffer.
   */
  virtual void addBufferFragment(BufferFragment& fragment) PURE;

  /**
   * Copy a string into the buffer.
   * @param data supplies the string to copy.
   */
  virtual void add(absl::string_view data) PURE;

  /**
   * Copy another buffer into this buffer.
   * @param data supplies the buffer to copy.
   */
  virtual void add(const Instance& data) PURE;

  /**
   * Prepend a string_view to the buffer.
   * @param data supplies the string_view to copy.
   */
  virtual void prepend(absl::string_view data) PURE;

  /**
   * Prepend data from another buffer to this buffer.
   * The supplied buffer is drained after this operation.
   * @param data supplies the buffer to copy.
   */
  virtual void prepend(Instance& data) PURE;

  /**
   * Copy out a section of the buffer.
   * @param start supplies the buffer index to start copying from.
   * @param size supplies the size of the output buffer.
   * @param data supplies the output buffer to fill.
   */
  virtual void copyOut(size_t start, uint64_t size, void* data) const PURE;

  /**
   * Drain data from the buffer.
   * @param size supplies the length of data to drain.
   */
  virtual void drain(uint64_t size) PURE;

  /**
   * Fetch the raw buffer slices.
   * @param max_slices supplies an optional limit on the number of slices to fetch, for performance.
   * @return RawSliceVector with non-empty slices in the buffer.
   */
  virtual RawSliceVector
  getRawSlices(absl::optional<uint64_t> max_slices = absl::nullopt) const PURE;

  /**
   * Fetch the valid data pointer and valid data length of the first non-zero-length
   * slice in the buffer.
   * @return RawSlice the first non-empty slice in the buffer, or {nullptr, 0} if the buffer
   * is empty.
   */
  virtual RawSlice frontSlice() const PURE;

  /**
   * Transfer ownership of the front slice to the caller. Must only be called if the
   * buffer is not empty otherwise the implementation will have undefined behavior.
   * If the underlying slice is immutable then the implementation must create and return
   * a mutable slice that has a copy of the immutable data.
   * @return pointer to SliceData object that wraps the front slice
   */
  virtual SliceDataPtr extractMutableFrontSlice() PURE;

  /**
   * @return uint64_t the total length of the buffer (not necessarily contiguous in memory).
   */
  virtual uint64_t length() const PURE;

  /**
   * @return a pointer to the first byte of data that has been linearized out to size bytes.
   */
  virtual void* linearize(uint32_t size) PURE;

  /**
   * Move a buffer into this buffer. As little copying is done as possible.
   * @param rhs supplies the buffer to move.
   */
  virtual void move(Instance& rhs) PURE;

  /**
   * Move a portion of a buffer into this buffer. As little copying is done as possible.
   * @param rhs supplies the buffer to move.
   * @param length supplies the amount of data to move.
   */
  virtual void move(Instance& rhs, uint64_t length) PURE;

  /**
   * Reserve space in the buffer for reading into. The amount of space reserved is determined
   * based on buffer settings and performance considerations.
   * @return a `Reservation`, on which `commit()` can be called, or which can
   *   be destructed to discard any resources in the `Reservation`.
   */
  virtual Reservation reserveForRead() PURE;

  /**
   * Reserve space in the buffer in a single slice.
   * @param length the exact length of the reservation.
   * @param separate_slice specifies whether the reserved space must be in a separate slice
   *   from any other data in this buffer.
   * @return a `ReservationSingleSlice` which has exactly one slice in it.
   */
  virtual ReservationSingleSlice reserveSingleSlice(uint64_t length,
                                                    bool separate_slice = false) PURE;

  /**
   * Search for an occurrence of data within the buffer.
   * @param data supplies the data to search for.
   * @param size supplies the length of the data to search for.
   * @param start supplies the starting index to search from.
   * @param length limits the search to specified number of bytes starting from start index.
   * When length value is zero, entire length of data from starting index to the end is searched.
   * @return the index where the match starts or -1 if there is no match.
   */
  virtual ssize_t search(const void* data, uint64_t size, size_t start, size_t length) const PURE;

  /**
   * Search for an occurrence of data within entire buffer.
   * @param data supplies the data to search for.
   * @param size supplies the length of the data to search for.
   * @param start supplies the starting index to search from.
   * @return the index where the match starts or -1 if there is no match.
   */
  ssize_t search(const void* data, uint64_t size, size_t start) const {
    return search(data, size, start, 0);
  }

  /**
   * Search for an occurrence of data at the start of a buffer.
   * @param data supplies the data to search for.
   * @return true if this buffer starts with data, false otherwise.
   */
  virtual bool startsWith(absl::string_view data) const PURE;

  /**
   * Constructs a flattened string from a buffer.
   * @return the flattened string.
   */
  virtual std::string toString() const PURE;

  /**
   * Copy an integer out of the buffer.
   * @param start supplies the buffer index to start copying from.
   * @param Size how many bytes to read out of the buffer.
   * @param Endianness specifies the byte order to use when decoding the integer.
   * @details Size parameter: Some protocols have integer fields whose size in bytes won't match the
   * size in bytes of C++'s integer types. Take a 3-byte integer field for example, which we want to
   * represent as a 32-bit (4 bytes) integer. One option to deal with that situation is to read 4
   * bytes from the buffer and ignore 1. There are a few problems with that solution, though.
   *   * The first problem is buffer underflow: there may not be more than Size bytes available
   * (say, last field in the payload), so that's an edge case to take into consideration.
   *   * The second problem is draining the buffer after reading. With the above solution we cannot
   *     read and discard in one go. We'd need to peek 4 bytes, ignore 1 and then drain 3. That not
   *     only looks hacky since the sizes don't match, but also produces less terse code and
   * requires the caller to propagate that logic to all call sites. Things complicate even further
   * when endianness is taken into consideration: should the most or least-significant bytes be
   * padded? Dealing with this situation requires a high level of care and attention to detail.
   * Properly calculating which bytes to discard and how to displace the data is not only error
   * prone, but also shifts to the caller a burden that could be solved in a much more generic,
   * transparent and well tested manner.
   *   * The last problem in the list is sign extension, which should be properly handled when
   * reading signed types with negative values. To make matters easier, the optional Size parameter
   * can be specified in those situations where there's a need to read less bytes than a C++'s
   * integer size in bytes. For the most common case when one needs to read exactly as many bytes as
   * the size of C++'s integer, this parameter can simply be omitted and it will be automatically
   * deduced from the size of the type T
   */
  template <typename T, ByteOrder Endianness = ByteOrder::Host, size_t Size = sizeof(T)>
  T peekInt(uint64_t start = 0) const {
    static_assert(Size <= sizeof(T), "requested size is bigger than integer being read");

    if (length() < start + Size) {
      ExceptionUtil::throwEnvoyException("buffer underflow");
    }

    constexpr const auto displacement = Endianness == ByteOrder::BigEndian ? sizeof(T) - Size : 0;

    auto result = static_cast<T>(0);
    constexpr const auto all_bits_enabled = static_cast<T>(~static_cast<T>(0));

    int8_t* bytes = reinterpret_cast<int8_t*>(std::addressof(result));
    copyOut(start, Size, &bytes[displacement]);

    constexpr const auto most_significant_read_byte =
        Endianness == ByteOrder::BigEndian ? displacement : Size - 1;

    // If Size == sizeof(T), we need to make sure we don't generate an invalid left shift
    // (e.g. int32 << 32), even though we know that that branch of the conditional will.
    // not be taken. Size % sizeof(T) gives us the correct left shift when Size < sizeof(T),
    // and generates a left shift of 0 bits when Size == sizeof(T)
    const auto sign_extension_bits =
        std::is_signed<T>::value && Size < sizeof(T) && bytes[most_significant_read_byte] < 0
            ? static_cast<T>(static_cast<typename std::make_unsigned<T>::type>(all_bits_enabled)
                             << ((Size % sizeof(T)) * CHAR_BIT))
            : static_cast<T>(0);

    return fromEndianness<Endianness>(static_cast<T>(result)) | sign_extension_bits;
  }

  /**
   * Copy a little endian integer out of the buffer.
   * @param start supplies the buffer index to start copying from.
   * @param Size how many bytes to read out of the buffer.
   */
  template <typename T, size_t Size = sizeof(T)> T peekLEInt(uint64_t start = 0) const {
    return peekInt<T, ByteOrder::LittleEndian, Size>(start);
  }

  /**
   * Copy a big endian integer out of the buffer.
   * @param start supplies the buffer index to start copying from.
   * @param Size how many bytes to read out of the buffer.
   */
  template <typename T, size_t Size = sizeof(T)> T peekBEInt(uint64_t start = 0) const {
    return peekInt<T, ByteOrder::BigEndian, Size>(start);
  }

  /**
   * Copy an integer out of the buffer and drain the read data.
   * @param Size how many bytes to read out of the buffer.
   * @param Endianness specifies the byte order to use when decoding the integer.
   */
  template <typename T, ByteOrder Endianness = ByteOrder::Host, size_t Size = sizeof(T)>
  T drainInt() {
    const auto result = peekInt<T, Endianness, Size>();
    drain(Size);
    return result;
  }

  /**
   * Copy a little endian integer out of the buffer and drain the read data.
   * @param Size how many bytes to read out of the buffer.
   */
  template <typename T, size_t Size = sizeof(T)> T drainLEInt() {
    return drainInt<T, ByteOrder::LittleEndian, Size>();
  }

  /**
   * Copy a big endian integer out of the buffer and drain the read data.
   * @param Size how many bytes to read out of the buffer.
   */
  template <typename T, size_t Size = sizeof(T)> T drainBEInt() {
    return drainInt<T, ByteOrder::BigEndian, Size>();
  }

  /**
   * Copy a byte into the buffer.
   * @param value supplies the byte to copy into the buffer.
   */
  void writeByte(uint8_t value) { add(std::addressof(value), 1); }

  /**
   * Copy value as a byte into the buffer.
   * @param value supplies the byte to copy into the buffer.
   */
  template <typename T> void writeByte(T value) { writeByte(static_cast<uint8_t>(value)); }

  /**
   * Copy an integer into the buffer.
   * @param value supplies the integer to copy into the buffer.
   * @param Size how many bytes to write from the requested integer.
   * @param Endianness specifies the byte order to use when encoding the integer.
   */
  template <ByteOrder Endianness = ByteOrder::Host, typename T, size_t Size = sizeof(T)>
  void writeInt(T value) {
    static_assert(Size <= sizeof(T), "requested size is bigger than integer being written");

    const auto data = toEndianness<Endianness>(value);
    constexpr const auto displacement = Endianness == ByteOrder::BigEndian ? sizeof(T) - Size : 0;
    add(reinterpret_cast<const char*>(std::addressof(data)) + displacement, Size);
  }

  /**
   * Copy an integer into the buffer in little endian byte order.
   * @param value supplies the integer to copy into the buffer.
   * @param Size how many bytes to write from the requested integer.
   */
  template <typename T, size_t Size = sizeof(T)> void writeLEInt(T value) {
    writeInt<ByteOrder::LittleEndian, T, Size>(value);
  }

  /**
   * Copy an integer into the buffer in big endian byte order.
   * @param value supplies the integer to copy into the buffer.
   * @param Size how many bytes to write from the requested integer.
   */
  template <typename T, size_t Size = sizeof(T)> void writeBEInt(T value) {
    writeInt<ByteOrder::BigEndian, T, Size>(value);
  }

  /**
   * Set the buffer's high watermark. The buffer's low watermark is implicitly set to half the high
   * watermark. Setting the high watermark to 0 disables watermark functionality.
   * @param watermark supplies the buffer high watermark size threshold, in bytes.
   */
  virtual void setWatermarks(uint32_t watermark) PURE;
  /**
   * Returns the configured high watermark. A return value of 0 indicates that watermark
   * functionality is disabled.
   */
  virtual uint32_t highWatermark() const PURE;
  /**
   * Determine if the buffer watermark trigger condition is currently set. The watermark trigger is
   * set when the buffer size exceeds the configured high watermark and is cleared once the buffer
   * size drops to the low watermark.
   * @return true if the buffer size once exceeded the high watermark and hasn't since dropped to
   * the low watermark.
   */
  virtual bool highWatermarkTriggered() const PURE;

private:
  friend Reservation;
  friend ReservationSingleSlice;

  /**
   * Called by a `Reservation` to commit `length` bytes of the
   * reservation.
   */
  virtual void commit(uint64_t length, absl::Span<RawSlice> slices,
                      ReservationSlicesOwnerPtr slices_owner) PURE;
};

using InstancePtr = std::unique_ptr<Instance>;

/**
 * A factory for creating buffers which call callbacks when reaching high and low watermarks.
 */
class WatermarkFactory {
public:
  virtual ~WatermarkFactory() = default;

  /**
   * Creates and returns a unique pointer to a new buffer.
   * @param below_low_watermark supplies a function to call if the buffer goes under a configured
   *   low watermark.
   * @param above_high_watermark supplies a function to call if the buffer goes over a configured
   *   high watermark.
   * @return a newly created InstancePtr.
   */
  virtual InstancePtr create(std::function<void()> below_low_watermark,
                             std::function<void()> above_high_watermark,
                             std::function<void()> above_overflow_watermark) PURE;
};

using WatermarkFactoryPtr = std::unique_ptr<WatermarkFactory>;
using WatermarkFactorySharedPtr = std::shared_ptr<WatermarkFactory>;

/**
 * Holds an in-progress addition to a buffer.
 *
 * @note For performance reasons, this class is passed by value to
 * avoid an extra allocation, so it cannot have any virtual methods.
 */
class Reservation final {
public:
  Reservation(Reservation&&) = default;
  ~Reservation() = default;

  /**
   * @return an array of `RawSlice` of length `numSlices()`.
   */
  RawSlice* slices() { return slices_.data(); }
  const RawSlice* slices() const { return slices_.data(); }

  /**
   * @return the number of slices present.
   */
  uint64_t numSlices() const { return slices_.size(); }

  /**
   * @return the total length of the Reservation.
   */
  uint64_t length() const { return length_; }

  /**
   * Commits some or all of the data in the reservation.
   * @param length supplies the number of bytes to commit. This must be
   *   less than or equal to the size of the `Reservation`.
   *
   * @note No other methods should be called on the object after `commit()` is called.
   */
  void commit(uint64_t length) {
    ENVOY_BUG(length <= length_, "commit() length must be <= size of the Reservation");
    ASSERT(length == 0 || !slices_.empty(),
           "Reservation.commit() called on empty Reservation; possible double-commit().");
    buffer_.commit(length, absl::MakeSpan(slices_), std::move(slices_owner_));
    length_ = 0;
    slices_.clear();
    ASSERT(slices_owner_ == nullptr);
  }

  // Tuned to allow reads of 128k, using 16k slices.
  static constexpr uint32_t MAX_SLICES_ = 8;

private:
  Reservation(Instance& buffer) : buffer_(buffer) {}

  // The buffer that created this `Reservation`.
  Instance& buffer_;

  // The combined length of all slices in the Reservation.
  uint64_t length_;

  // The RawSlices in the reservation, usable by operations such as `::readv()`.
  absl::InlinedVector<RawSlice, MAX_SLICES_> slices_;

  // An owner that can be set by the creator of the `Reservation` to free slices upon
  // destruction.
  ReservationSlicesOwnerPtr slices_owner_;

public:
  // The following are for use only by implementations of Buffer. Because c++
  // doesn't allow inheritance of friendship, these are just trying to make
  // misuse easy to spot in a code review.
  static Reservation bufferImplUseOnlyConstruct(Instance& buffer) { return Reservation(buffer); }
  decltype(slices_)& bufferImplUseOnlySlices() { return slices_; }
  ReservationSlicesOwnerPtr& bufferImplUseOnlySlicesOwner() { return slices_owner_; }
  void bufferImplUseOnlySetLength(uint64_t length) { length_ = length; }
};

/**
 * Holds an in-progress addition to a buffer, holding only a single slice.
 *
 * @note For performance reasons, this class is passed by value to
 * avoid an extra allocation, so it cannot have any virtual methods.
 */
class ReservationSingleSlice final {
public:
  ReservationSingleSlice(ReservationSingleSlice&&) = default;
  ~ReservationSingleSlice() = default;

  /**
   * @return the slice in the Reservation.
   */
  RawSlice slice() const { return slice_; }

  /**
   * @return the total length of the Reservation.
   */
  uint64_t length() const { return slice_.len_; }

  /**
   * Commits some or all of the data in the reservation.
   * @param length supplies the number of bytes to commit. This must be
   *   less than or equal to the size of the `Reservation`.
   *
   * @note No other methods should be called on the object after `commit()` is called.
   */
  void commit(uint64_t length) {
    ENVOY_BUG(length <= slice_.len_, "commit() length must be <= size of the Reservation");
    ASSERT(length == 0 || slice_.mem_ != nullptr,
           "Reservation.commit() called on empty Reservation; possible double-commit().");
    buffer_.commit(length, absl::MakeSpan(&slice_, 1), std::move(slice_owner_));
    slice_ = {nullptr, 0};
    ASSERT(slice_owner_ == nullptr);
  }

private:
  ReservationSingleSlice(Instance& buffer) : buffer_(buffer) {}

  // The buffer that created this `Reservation`.
  Instance& buffer_;

  // The RawSlice in the reservation, usable by anything needing the raw pointer
  // and length to read into.
  RawSlice slice_{};

  // An owner that can be set by the creator of the `ReservationSingleSlice` to free the slice upon
  // destruction.
  ReservationSlicesOwnerPtr slice_owner_;

public:
  // The following are for use only by implementations of Buffer. Because c++
  // doesn't allow inheritance of friendship, these are just trying to make
  // misuse easy to spot in a code review.
  static ReservationSingleSlice bufferImplUseOnlyConstruct(Instance& buffer) {
    return ReservationSingleSlice(buffer);
  }
  RawSlice& bufferImplUseOnlySlice() { return slice_; }
  ReservationSlicesOwnerPtr& bufferImplUseOnlySliceOwner() { return slice_owner_; }
};

} // namespace Buffer
} // namespace Envoy
