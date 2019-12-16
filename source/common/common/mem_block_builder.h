#pragma once

#include <memory>

#include "common/common/assert.h"

#include "absl/types/span.h"

namespace Envoy {

// Manages a block of raw memory for objects of type T. T is generally expected
// to be a POD, where it makes sense to memcpy over it. This class carries extra
// member variables for tracking size, and a write-pointer to support safe
// appends.
//
// MemBlockBuilder is used to safely write blocks of data into a memory
// buffer. Due to two extra member variables, it is not optimal for storing in
// data structures. The intended usage model is to release the raw assembled
// memory block from the MemBlockBuilder for efficient storage.
//
// The goal for this class is to provide a usage model to replace direct usage
// of memcpy with a pattern that is easy to validate for correctness by
// inspection, asserts, and fuzzing, but when compiled for optimization is
// roughly as efficient as raw memcpy.
template <class T> class MemBlockBuilder {
public:
  // Constructs a MemBlockBuilder allowing for 'capacity' instances of T.
  explicit MemBlockBuilder(uint64_t capacity)
      : data_(std::make_unique<T[]>(capacity)), write_span_(data_.get(), capacity) {}
  MemBlockBuilder() {}

  /**
   * Allocates (or reallocates) memory for the MemBlockBuilder to make it the
   * specified capacity. This does not have resize semantics; when setCapacity()
   * is called any previous contents are erased.
   *
   * @param capacity The number of memory elements to allocate.
   */
  void setCapacity(uint64_t capacity) {
    setCapacityHelper(capacity, std::make_unique<T[]>(capacity));
  }

  /**
   * @return the capacity.
   */
  uint64_t capacity() const { return write_span_.size() + write_span_.data() - data_.get(); }

  /**
   * Appends a single object of type T, moving an internal write-pointer
   * forward. Asserts that there is room to write the object when compiled
   * for debug.
   *
   * @param object the object to append.
   */
  void appendOne(T object) {
    SECURITY_ASSERT(write_span_.size() >= 1, "insufficient capacity");
    *write_span_.data() = object;
    write_span_.remove_prefix(1);
  }

  /**
   * Appends raw data specified as a span, moving an internal write-pointer
   * forward. Asserts that there is room to write the block. It is the caller's
   * responsibility to ensure that the input data is valid.
   *
   * @param data The span of objects to insert.
   */
  void appendData(absl::Span<const T> data) {
    uint64_t size = data.size();
    SECURITY_ASSERT(write_span_.size() >= size, "insufficient capacity");
    if (size == 0) {
      return;
    }
    memcpy(write_span_.data(), data.data(), size * sizeof(T));
    write_span_.remove_prefix(size);
  }

  /**
   * Appends the contents of another memory block to this one.
   *
   * @param src the block to append.
   */
  void appendBlock(const MemBlockBuilder& src) { appendData(src.span()); }

  /**
   * @return the number of elements remaining in the MemBlockBuilder.
   */
  uint64_t capacityRemaining() const { return write_span_.size(); }

  /**
   * Empties the contents of this.
   */
  void reset() { setCapacityHelper(0, std::unique_ptr<T[]>(nullptr)); }

  /**
   * Returns the underlying storage as a unique pointer, clearing this.
   *
   * @return the transferred storage.
   */
  std::unique_ptr<T[]> release() {
    write_span_ = absl::MakeSpan(static_cast<T*>(nullptr), 0);
    return std::move(data_);
  }

  /**
   * @return the populated data as an absl::Span.
   */
  absl::Span<T> span() const { return absl::MakeSpan(data_.get(), write_span_.data()); }

  /**
   * @return The number of elements the have been added to the builder.
   */
  uint64_t size() const { return write_span_.data() - data_.get(); }

private:
  void setCapacityHelper(uint64_t capacity, std::unique_ptr<T[]> data) {
    data_ = std::move(data);
    write_span_ = absl::MakeSpan(data_.get(), capacity);
  }

  std::unique_ptr<T[]> data_;
  absl::Span<T> write_span_;
};

} // namespace Envoy
