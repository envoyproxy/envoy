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
#include "quiche/quic/platform/api/quic_logging.h"

namespace quic {

// Implements the interface required by
// https://quiche.googlesource.com/quiche/+/refs/heads/master/quic/platform/api/quic_mem_slice.h
class QuicMemSliceImpl {
public:
  // Constructs an empty QuicMemSliceImpl.
  QuicMemSliceImpl() = default;

  // A helper function to construct a buffer fragment and the buffer of given length the fragment
  // points to with the guarantee that buffer and fragment are both aligned according to
  // max_align_t.
  static Envoy::Buffer::BufferFragmentImpl*
  allocateBufferAndFragment(QuicBufferAllocator* allocator, size_t length) {
    auto bundle = reinterpret_cast<BufferFragmentBundle*>(
        allocator->New(sizeof(BufferFragmentBundle) + length));
    Envoy::Buffer::BufferFragmentImpl& fragment = bundle->fragment_with_padding.fragment;
    return new (&fragment) Envoy::Buffer::BufferFragmentImpl(
        bundle->buffer, length,
        [allocator](const void*, size_t, const Envoy::Buffer::BufferFragmentImpl* frag) {
          // Delete will deallocate the bundle of fragment and buffer.
          allocator->Delete(const_cast<char*>(reinterpret_cast<const char*>(frag)));
        });
  }

  // Constructs a QuicMemSliceImpl by let |allocator| allocate a data buffer of
  // |length|.
  QuicMemSliceImpl(QuicBufferAllocator* allocator, size_t length) {
    Envoy::Buffer::BufferFragmentImpl* fragment = allocateBufferAndFragment(allocator, length);
    single_slice_buffer_.addBufferFragment(*fragment);
  }

  // Constructs a QuicMemSliceImpl from a Buffer::Instance with first |length| bytes in it.
  // Data will be moved from |buffer| to this mem slice.
  // Prerequisite: |buffer| has at least |length| bytes of data and not empty.
  explicit QuicMemSliceImpl(Envoy::Buffer::Instance& buffer, size_t length) {
    DCHECK_EQ(length, firstSliceLength(buffer));
    single_slice_buffer_.move(buffer, length);
    ASSERT(single_slice_buffer_.getRawSlices(nullptr, 0) == 1);
  }

  QuicMemSliceImpl(const QuicMemSliceImpl& other) = delete;
  QuicMemSliceImpl& operator=(const QuicMemSliceImpl& other) = delete;

  // Move constructors. |other| will not hold a reference to the data buffer
  // after this call completes.
  QuicMemSliceImpl(QuicMemSliceImpl&& other) { *this = std::move(other); }

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
  // Used to align both fragment and buffer at max aligned address.
  struct BufferFragmentBundle {
    // Wrap fragment in a nested struct so that it can be padded according to
    // its alignment requirement: max_align_t. This ensures buffer to start at a
    // max aligned address.
    struct {
      alignas(std::max_align_t) Envoy::Buffer::BufferFragmentImpl fragment;
    } fragment_with_padding;
    char buffer[0];
  };

  // Prerequisite: buffer has at least one slice.
  size_t firstSliceLength(Envoy::Buffer::Instance& buffer) {
    Envoy::Buffer::RawSlice slice;
    ASSERT(buffer.getRawSlices(&slice, 1) != 0);
    return slice.len_;
  }

  Envoy::Buffer::OwnedImpl single_slice_buffer_;
};

} // namespace quic
