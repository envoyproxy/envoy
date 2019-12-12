#pragma once

#include <memory>
#include <vector>

#include "common/common/assert.h"

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
      : data_(std::make_unique<T[]>(capacity)), capacity_(capacity), capacity_remaining_(capacity),
        write_ptr_(data_.get()) {}
  MemBlockBuilder() : capacity_(0), capacity_remaining_(0), write_ptr_(nullptr) {}

  /**
   * Populates (or repopulates) the MemBlockBuilder to make it the specified
   * capacity. This does not have resize semantics; when populate() is called any
   * previous contents are erased.
   *
   * @param capacity The number of memory elements to allocate.
   */
  void populate(uint64_t capacity) {
    data_ = std::make_unique<T[]>(capacity);
    capacity_ = capacity;
    capacity_remaining_ = capacity;
    write_ptr_ = data_.get();
  }

  /**
   * @return the capacity.
   */
  uint64_t capacity() const { return capacity_; }

  /**
   * Appends a single object of type T, moving an internal write-pointer
   * forward. Asserts that there is room to write the object when compiled
   * for debug.
   *
   * @param object the object to append.
   */
  void appendOne(T object) {
    RELEASE_ASSERT(capacity_remaining_ >= 1, "insufficient capacity");
    ASSERT(write_ptr_ < (data_.get() + capacity_));
    *write_ptr_++ = object;
    --capacity_remaining_;
  }

  /**
   * Appends raw data, moving an internal write-pointer forward. Asserts
   * that there is room to write the block when compiled for debug. It is
   * the caller's responsibility to ensure that the input data is valid.
   *
   * @param data The block of objects to insert.
   * @param size The number of elements in the block.
   */
  void appendData(const T* data, uint64_t size) {
    RELEASE_ASSERT(capacity_remaining_ >= size, "insufficient capacity");
    ASSERT((write_ptr_ + size) <= (data_.get() + capacity_));
    if (size != 0) {
      memcpy(write_ptr_, data, size * sizeof(T));
    }
    write_ptr_ += size;
    capacity_remaining_ -= size;
  }

  /**
   * Appends the contents of another memory block to this one.
   *
   * @param src the block to append.
   */
  void appendBlock(const MemBlockBuilder& src) { appendData(src.data_.get(), src.size()); }

  /**
   * @return the number of elements remaining in the MemBlockBuilder.
   */
  uint64_t capacityRemaining() const { return capacity_remaining_; }

  /**
   * Empties the contents of this.
   */
  void reset() {
    data_.reset();
    capacity_ = 0;
    capacity_remaining_ = 0;
    write_ptr_ = nullptr;
  }

  /**
   * Returns the underlying storage as a unique pointer, clearing this.
   *
   * @return the transferred storage.
   */
  std::unique_ptr<T[]> release() {
    write_ptr_ = nullptr;
    capacity_ = 0;
    capacity_remaining_ = 0;
    return std::move(data_);
  }

  /**
   * @return the populated data as a vector.
   *
   * This is exposed to help with unit testing.
   */
  std::vector<T> toVector() const { return std::vector<T>(data_.get(), write_ptr_); }

  /**
   * @return The number of elements the have been added to the builder.
   */
  uint64_t size() const { return write_ptr_ - data_.get(); }

private:
  std::unique_ptr<T[]> data_;
  uint64_t capacity_;
  uint64_t capacity_remaining_;
  T* write_ptr_;
};

} // namespace Envoy
