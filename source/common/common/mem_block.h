#pragma once

#include <memory>

#include "common/common/assert.h"

namespace Envoy {

template<class T> class MemBlock {
 public:
  // Constructs a MemBlock wrapper to an existing memory block. The caller
  // is responsible for ensuring that 'data' has size 'size'.
  explicit MemBlock(uint64_t size) : data_(std::make_unique<T[]>(size)), size_(size),
                                     next_(data_.get()) {}
  explicit MemBlock() : size_(0), next_(nullptr) {}

  void populate(uint64_t size) {
    data_ = std::make_unique<T[]>(size);
    size_ = size;
    next_ = data_.get();
  }

  T* data() { return data_.get(); }

  void copyFrom(const MemBlock& src, uint64_t size,
                uint64_t src_offset = 0, uint64_t dst_offset = 0) {
    ASSERT(src_offset + size <= src.size_);
    ASSERT(dst_offset + size <= size_);
    memcpy(data_.get() + dst_offset, src.data_ + src_offset, size);
    next_ += size;
  }

  void dangerousCopyFrom(const T* source, uint64_t size, uint64_t dst_offset = 0) {
    ASSERT(dst_offset + size <= size_);
    memcpy(data_.get() + dst_offset, source, size);
    next_ += size;
  }

  void push_back(T byte) {
    ASSERT(bytesRemaining() >= 1);
    *next_++ = byte;
  }

  uint64_t bytesRemaining() const {
    return (data_.get() + size_) - next_;
  }

  void append(const T* byte, uint64_t size) {
    ASSERT(bytesRemaining() >= size);
    memcpy(next_, byte, size);
    next_ += size;
  }

  void append(const MemBlock& src) {
    ASSERT(bytesRemaining() >= src.size_);
    memcpy(next_, src.data_.get(), src.size_);
    next_ += src.size_;
  }

  void reset() {
    data_.reset();
    size_ = 0;
    next_ = nullptr;
  }

  std::unique_ptr<T[]> release() { return std::move(data_); }

 private:
  std::unique_ptr<T[]> data_;
  uint64_t size_;
  uint8_t* next_;
};

} // namespace Envoy
