#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/api/os_sys_calls.h"
#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/common/pure.h"

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
   * Commit a set of slices originally obtained from reserve(). The number of slices should match
   * the number obtained from reserve(). The size of each slice can also be altered. Commit must
   * occur once following a reserve() without any mutating operations in between other than to the
   * iovecs len_ fields.
   * @param iovecs supplies the array of slices to commit.
   * @param num_iovecs supplies the size of the slices array.
   */
  virtual void commit(RawSlice* iovecs, uint64_t num_iovecs) PURE;

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
   * Reserve space in the buffer.
   * @param length supplies the amount of space to reserve.
   * @param iovecs supplies the slices to fill with reserved memory.
   * @param num_iovecs supplies the size of the slices array.
   * @return the number of iovecs used to reserve the space.
   */
  virtual uint64_t reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) PURE;

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
  T peekInt(uint64_t start = 0) {
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
  template <typename T, size_t Size = sizeof(T)> T peekLEInt(uint64_t start = 0) {
    return peekInt<T, ByteOrder::LittleEndian, Size>(start);
  }

  /**
   * Copy a big endian integer out of the buffer.
   * @param start supplies the buffer index to start copying from.
   * @param Size how many bytes to read out of the buffer.
   */
  template <typename T, size_t Size = sizeof(T)> T peekBEInt(uint64_t start = 0) {
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

} // namespace Buffer
} // namespace Envoy
