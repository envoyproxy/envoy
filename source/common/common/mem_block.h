#pragma once

#include <memory>

#include "common/common/assert.h"

namespace Envoy {

template <class T> class MemBlock {
public:
  // Constructs a MemBlock wrapper to an existing memory block. The caller
  // is responsible for ensuring that 'data' has size 'size'.
  explicit MemBlock(uint64_t size)
      : data_(std::make_unique<T[]>(size)), size_(size), next_(data_.get()) {}
  explicit MemBlock() : size_(0), next_(nullptr) {}

  void populate(uint64_t size) {
    data_ = std::make_unique<T[]>(size);
    size_ = size;
    next_ = data_.get();
  }

  bool empty() const { return data_ == nullptr; }

  void push_back(T byte) {
    ASSERT(bytesRemaining() >= 1);
    *next_++ = byte;
  }

  uint64_t bytesRemaining() const { return (data_.get() + size_) - next_; }

  void append(const T* byte, uint64_t size) {
    ASSERT(bytesRemaining() >= size);
    memcpy(next_, byte, size);
    next_ += size;
  }

  void append(const MemBlock& src) { append(src.data_.get(), src.size_); }

  void reset() {
    data_.reset();
    size_ = 0;
    next_ = nullptr;
  }

  std::unique_ptr<T[]> release() {
    next_ = nullptr;
    size_ = 0;
    return std::move(data_);
  }

private:
  std::unique_ptr<T[]> data_;
  uint64_t size_;
  uint8_t* next_;
};

} // namespace Envoy
