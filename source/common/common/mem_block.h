#pragma once

#include <memory>

#include "common/common/assert.h"

namespace Envoy {

// Manages a block of raw memory for objects of type T. T must be
// empty-constructible. This class carries extra member variables
// for tracking size, and a write-pointer to support safe appends.
//
// MemBlock is used to efficiently write blocks of data into a memory
// buffer. Due to the extra member variables, it is not optimal for
// storing in data structures. Instead, the raw assembled memory block
// is released from the MemBlock after assembly.
template <class T> class MemBlock {
public:
  // Constructs a MemBlock of the specified size.
  explicit MemBlock(uint64_t size)
      : data_(std::make_unique<T[]>(size)), capacity_(size), write_ptr_(data_.get()) {}
  MemBlock() : capacity_(0), write_ptr_(nullptr) {}

  /**
   * Populates (or repopulates) the MemBlock to make it the specified size.
   * This does not have resize semantics; when populate() is called any
   * previous contents are erased.
   *
   * @param size The number of memory elements to allocate.
   */
  void populate(uint64_t size) {
    data_ = std::make_unique<T[]>(size);
    capacity_ = size;
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
    ASSERT(capacityRemaining() >= 1);
    *write_ptr_++ = object;
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
    ASSERT(capacityRemaining() >= size);
    memcpy(write_ptr_, data, size * sizeof(T));
    write_ptr_ += size;
  }

  /**
   * Appends the contents of another memory block to this one.
   *
   * @param src the block to append.
   */
  void appendBlock(const MemBlock& src) { appendData(src.data_.get(), src.size()); }

  /**
   * @return the number of elements remaining in the MemBlock.
   */
  uint64_t capacityRemaining() const { return (data_.get() + capacity_) - write_ptr_; }

  /**
   * Empties the contents of this.
   */
  void reset() {
    data_.reset();
    capacity_ = 0;
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
    return std::move(data_);
  }

  /**
   * @return the populated data as a vector.
   *
   * This is exposed to help with unit testing.
   */
  std::vector<T> toVector() const { return std::vector<T>(data_.get(), write_ptr_); }

private:
  uint64_t size() const { return write_ptr_ - data_.get(); }

  std::unique_ptr<T[]> data_;
  uint64_t capacity_;
  T* write_ptr_;
};

} // namespace Envoy
