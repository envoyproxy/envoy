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

// Implements the interface required by
// https://quiche.googlesource.com/quiche/+/refs/heads/master/quic/platform/api/quic_mem_slice.h
// Note the Quic plugin strategy relies on link-time binding rather than inheritance.
class QuicMemSliceImpl {
public:
  // Constructs an empty QuicMemSliceImpl.
  QuicMemSliceImpl() = default;

  // Constructs a QuicMemSliceImpl by let |allocator| allocate a data buffer of
  // |length|.
  QuicMemSliceImpl(QuicBufferAllocator* allocator, size_t length)
      : slice_(new Envoy::Buffer::OwnedImpl()) {
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
  explicit QuicMemSliceImpl(std::shared_ptr<Envoy::Buffer::Instance> slice)
      : slice_(std::move(slice)) {
    ASSERT(slice_->getRawSlices(nullptr, 0) == 1);
  }

  QuicMemSliceImpl(const QuicMemSliceImpl& other) = delete;
  QuicMemSliceImpl& operator=(const QuicMemSliceImpl& other) = delete;

  // Move constructors. |other| will not hold a reference to the data buffer
  // after this call completes.
  QuicMemSliceImpl(QuicMemSliceImpl&& other) : slice_(std::move(other.slice_)) { other.Reset(); }

  QuicMemSliceImpl& operator=(QuicMemSliceImpl&& other) {
    if (this != &other) {
      slice_ = std::move(other.slice_);
      other.Reset();
    }
    return *this;
  }

  // Below methods implements interface needed by QuicMemSlice.
  void Reset() { slice_ = nullptr; }

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
  std::shared_ptr<Envoy::Buffer::Instance> slice_;
};

} // namespace quic
