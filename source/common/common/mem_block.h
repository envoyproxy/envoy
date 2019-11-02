#pragma once

#include <memory>

#include "common/common/assert.h"

namespace Envoy {

// Manages a block of raw memory for objects of type T. T must be
// empty-constructible.
template <class T> class MemBlock {
public:
  // Constructs a MemBlock of the specified size.
  explicit MemBlock(uint64_t size)
      : data_(std::make_unique<T[]>(size)), size_(size), write_ptr_(data_.get()) {}
  MemBlock() : size_(0), write_ptr_(nullptr) {}

  // Populates (or repopulates) the MemBlock to make it the specified size.
  // This does not have resize semantics; when populate() is called any
  // previous contents are erased.
  void populate(uint64_t size) {
    data_ = std::make_unique<T[]>(size);
    size_ = size;
    write_ptr_ = data_.get();
  }

  // Returns whether the block has been populated.
  bool empty() const { return data_ == nullptr; }

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
  void appendBlock(const MemBlock& src) { appendData(src.data_.get(), src.size_); }

  /**
   * @return the number of elements remaining in the MemBlock.
   */
  uint64_t capacityRemaining() const { return (data_.get() + size_) - write_ptr_; }

  /**
   * Empties the contents of this.
   */
  void reset() {
    data_.reset();
    size_ = 0;
    write_ptr_ = nullptr;
  }

  /**
   * Returns the underlying storage as a unique pointer, clearing this.
   *
   * @return The transferred storage.
   */
  std::unique_ptr<T[]> release() {
    write_ptr_ = nullptr;
    size_ = 0;
    return std::move(data_);
  }

  /**
   * @return read-only access to the data.
   */
  const T* data() const { return data_.get(); }

  /**
   * This is exposed to help with unit testing.
   *
   * @return the populated data as a vector.
   */
  std::vector<T> toVector() const { return std::vector<T>(data_.get(), write_ptr_); }

private:
  std::unique_ptr<T[]> data_;
  uint64_t size_;
  uint8_t* write_ptr_;
};

} // namespace Envoy
