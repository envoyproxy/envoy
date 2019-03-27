#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <iostream>
#include <memory>

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"

#include "quiche/quic/core/quic_buffer_allocator.h"

namespace quic {

class QuicMemSliceImpl {
public:
  // Constructs an empty QuicMemSliceImpl.
  QuicMemSliceImpl() = default;

  // Constructs a QuicMemSliceImpl by let |allocator| allocate a data buffer of
  // |length|.
  QuicMemSliceImpl(QuicBufferAllocator* allocator, size_t length)
      : length_(length), slice_(new Envoy::Buffer::OwnedImpl()) {
    auto fragment = new Envoy::Buffer::BufferFragmentImpl(
        allocator->New(length), length,
        [allocator](const void* data, size_t, const Envoy::Buffer::BufferFragmentImpl* fragment) {
          allocator->Delete(const_cast<char*>(static_cast<const char*>(data)));
          delete fragment;
        });
    slice_->addBufferFragment(*fragment);
  }

  // Constructs a QuicMemSliceImpl from a Buffer::Instance whose getRawSlices()
  // returns only 1 slice.
  QuicMemSliceImpl(std::shared_ptr<Envoy::Buffer::Instance> slice)
      : length_(slice->length()), slice_(std::move(slice)) {
    ASSERT(slice_->getRawSlices(nullptr, 0) == 1);
  }

  QuicMemSliceImpl(const QuicMemSliceImpl& other) = delete;
  QuicMemSliceImpl& operator=(const QuicMemSliceImpl& other) = delete;

  // Move constructors. |other| will not hold a reference to the data buffer
  // after this call completes.
  QuicMemSliceImpl(QuicMemSliceImpl&& other)
      : length_(other.length_), slice_(std::move(other.slice_)) {
    other.Reset();
  }

  QuicMemSliceImpl& operator=(QuicMemSliceImpl&& other) {
    if (this != &other) {
      length_ = other.length_;
      slice_ = std::move(other.slice_);
      other.Reset();
    }
    return *this;
  }

  void Reset() {
    length_ = 0;
    slice_ = nullptr;
  }

  // Returns a char pointer to the one and only slice in buffer.
  const char* data() const {
    if (slice_ == nullptr) {
      return nullptr;
    }
    Envoy::Buffer::RawSlice out;
    ASSERT(slice_->getRawSlices(&out, 1) == 1);
    return static_cast<const char*>(out.mem_);
  }

  size_t length() const { return slice_ == nullptr ? 0 : slice_->length(); }

  bool empty() const { return length() == 0; }

private:
  size_t length_;
  std::shared_ptr<Envoy::Buffer::Instance> slice_;
};

} // namespace quic
