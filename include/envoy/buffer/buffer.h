#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/api/os_sys_calls.h"
#include "envoy/common/exception.h"
#include "envoy/common/pure.h"

#include "common/common/byte_order.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Buffer {

/**
 * A raw memory data slice including location and length.
 */
struct RawSlice {
  void* mem_ = nullptr;
  size_t len_ = 0;
};

/**
 * A wrapper class to facilitate passing in externally owned data to a buffer via addBufferFragment.
 * When the buffer no longer needs the data passed in through a fragment, it calls done() on it.
 */
class BufferFragment {
public:
  /**
   * @return const void* a pointer to the referenced data.
   */
  virtual const void* data() const PURE;

  /**
   * @return size_t the size of the referenced data.
   */
  virtual size_t size() const PURE;

  /**
   * Called by a buffer when the refernced data is no longer needed.
   */
  virtual void done() PURE;

protected:
  virtual ~BufferFragment() {}
};

/**
 * A basic buffer abstraction.
 */
class Instance {
public:
  virtual ~Instance() {}

  /**
   * Copy data into the buffer.
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
  virtual void add(const std::string& data) PURE;

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
   * Commit a set of slices originally obtained from reserve(). The number of slices can be
   * different from the number obtained from reserve(). The size of each slice can also be altered.
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
   * Fetch the raw buffer slices. This routine is optimized for performance.
   * @param out supplies an array of RawSlice objects to fill.
   * @param out_size supplies the size of out.
   * @return the actual number of slices needed, which may be greater than out_size. Passing
   *         nullptr for out and 0 for out_size will just return the size of the array needed
   *         to capture all of the slice data.
   * TODO(mattklein123): WARNING: The underlying implementation of this function currently uses
   * libevent's evbuffer. It has the infuriating property where calling getRawSlices(nullptr, 0)
   * will return the slices that include all of the buffer data, but not any empty slices at the
   * end. However, calling getRawSlices(iovec, SOME_CONST), WILL return potentially empty slices
   * beyond the end of the buffer. Code that is trying to avoid stack overflow by limiting the
   * number of returned slices needs to deal with this. When we get rid of evbuffer we can rework
   * all of this.
   */
  virtual uint64_t getRawSlices(RawSlice* out, uint64_t out_size) const PURE;

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
   * Read from a file descriptor directly into the buffer.
   * @param fd supplies the descriptor to read from.
   * @param max_length supplies the maximum length to read.
   * @return a Api::SysCallIntResult with rc_ = the number of bytes read if successful, or rc_ = -1
   *   for failure. If the call is successful, errno_ shouldn't be used.
   */
  virtual Api::SysCallIntResult read(int fd, uint64_t max_length) PURE;

  /**
   * Reserve space in the buffer.
   * @param length supplies the amount of space to reserve.
   * @param iovecs supplies the slices to fill with reserved memory.
   * @param num_iovecs supplies the size of the slices array.
   * @return the number of iovecs used to reserve the space.
   */
  virtual uint64_t reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) PURE;

  /**
   * Search for an occurrence of a buffer within the larger buffer.
   * @param data supplies the data to search for.
   * @param size supplies the length of the data to search for.
   * @param start supplies the starting index to search from.
   * @return the index where the match starts or -1 if there is no match.
   */
  virtual ssize_t search(const void* data, uint64_t size, size_t start) const PURE;

  /**
   * Constructs a flattened string from a buffer.
   * @return the flattened string.
   */
  virtual std::string toString() const PURE;

  /**
   * Write the buffer out to a file descriptor.
   * @param fd supplies the descriptor to write to.
   * @return a Api::SysCallIntResult with rc_ = the number of bytes written if successful, or rc_ =
   * -1 for failure. If the call is successful, errno_ shouldn't be used.
   */
  virtual Api::SysCallIntResult write(int fd) PURE;

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
      throw EnvoyException("buffer underflow");
    }

    constexpr const auto displacement = Endianness == ByteOrder::BigEndian ? sizeof(T) - Size : 0;

    auto result = static_cast<T>(0);
    constexpr const auto all_bits_enabled = static_cast<T>(~static_cast<T>(0));

    int8_t* bytes = reinterpret_cast<int8_t*>(std::addressof(result));
    copyOut(start, Size, &bytes[displacement]);

    constexpr const auto most_significant_read_byte =
        Endianness == ByteOrder::BigEndian ? displacement : Size - 1;

    const auto sign_extension_bits =
        std::is_signed<T>::value && Size < sizeof(T) && bytes[most_significant_read_byte] < 0
            ? static_cast<T>(static_cast<typename std::make_unsigned<T>::type>(all_bits_enabled)
                             << (Size * CHAR_BIT))
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

typedef std::unique_ptr<Instance> InstancePtr;

/**
 * A factory for creating buffers which call callbacks when reaching high and low watermarks.
 */
class WatermarkFactory {
public:
  virtual ~WatermarkFactory() {}

  /**
   * Creates and returns a unique pointer to a new buffer.
   * @param below_low_watermark supplies a function to call if the buffer goes under a configured
   *   low watermark.
   * @param above_high_watermark supplies a function to call if the buffer goes over a configured
   *   high watermark.
   * @return a newly created InstancePtr.
   */
  virtual InstancePtr create(std::function<void()> below_low_watermark,
                             std::function<void()> above_high_watermark) PURE;
};

typedef std::unique_ptr<WatermarkFactory> WatermarkFactoryPtr;

} // namespace Buffer
} // namespace Envoy
