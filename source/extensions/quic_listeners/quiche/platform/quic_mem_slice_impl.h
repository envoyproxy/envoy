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
  QuicMemSliceImpl(QuicBufferAllocator* allocator, size_t length) {
    size_t total_length = sizeof(Envoy::Buffer::BufferFragmentImpl) + length;
    char* mem = allocator->New(total_length);
    auto fragment = new (mem + length) Envoy::Buffer::BufferFragmentImpl(
        mem, length,
        [allocator](const void* data, size_t, const Envoy::Buffer::BufferFragmentImpl*) {
          allocator->Delete(const_cast<char*>(static_cast<const char*>(data)));
        });
    single_slice_buffer_.addBufferFragment(*fragment);
  }

  // Constructs a QuicMemSliceImpl from a Buffer::Instance whose getRawSlices()
  // returns only 1 slice. Data will be moved from |slice|.
  explicit QuicMemSliceImpl(Envoy::Buffer::Instance& slice) {
    ASSERT(slice.getRawSlices(nullptr, 0) > 1);
    single_slice_buffer_.move(slice);
  }

  QuicMemSliceImpl(const QuicMemSliceImpl& other) = delete;
  QuicMemSliceImpl& operator=(const QuicMemSliceImpl& other) = delete;

  // Move constructors. |other| will not hold a reference to the data buffer
  // after this call completes.
  QuicMemSliceImpl(QuicMemSliceImpl&& other) {
    single_slice_buffer_.move(other.single_slice_buffer_);
    other.Reset();
  }

  QuicMemSliceImpl& operator=(QuicMemSliceImpl&& other) {
    if (this != &other) {
      single_slice_buffer_.move(other.single_slice_buffer_);
    }
    return *this;
  }

  // Below methods implements interface needed by QuicMemSlice.
  void Reset() { single_slice_buffer_.drain(length()); }

  // Returns a char pointer to the one and only slice in buffer.
  const char* data() const {
    Envoy::Buffer::RawSlice out;
    ASSERT(single_slice_buffer_.getRawSlices(&out, 1) <= 1);
    return static_cast<const char*>(out.mem_);
  }

  size_t length() const { return single_slice_buffer_.length(); }
  bool empty() const { return length() == 0; }

private:
  Envoy::Buffer::OwnedImpl single_slice_buffer_;
};

} // namespace quic
