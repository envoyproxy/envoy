#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/api/os_sys_calls.h"
#include "envoy/common/pure.h"

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
   * Search for an occurence of a buffer within the larger buffer.
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
