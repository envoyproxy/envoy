#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <cstddef>
#include <iostream>
#include <memory>

#include "common/buffer/buffer_impl.h"

#include "quiche/quic/core/quic_buffer_allocator.h"

namespace quic {

// Implements the interface required by
// https://quiche.googlesource.com/quiche/+/refs/heads/master/quic/platform/api/quic_mem_slice.h
class QuicMemSliceImpl {
public:
  // Constructs an empty QuicMemSliceImpl.
  QuicMemSliceImpl() = default;

  // Constructs a buffer fragment and the buffer of given length the fragment points to with the
  // guarantee that buffer and fragment are both aligned according to max_align_t.
  static Envoy::Buffer::BufferFragmentImpl*
  allocateBufferAndFragment(QuicBufferAllocator* allocator, size_t length);

  // Constructs a QuicMemSliceImpl by let |allocator| allocate a data buffer of
  // |length|.
  QuicMemSliceImpl(QuicBufferAllocator* allocator, size_t length);

  // Constructs a QuicMemSliceImpl from a Buffer::Instance with first |length| bytes in it.
  // Data will be moved from |buffer| to this mem slice.
  // Prerequisite: |buffer| has at least |length| bytes of data and not empty.
  explicit QuicMemSliceImpl(Envoy::Buffer::Instance& buffer, size_t length);

  QuicMemSliceImpl(const QuicMemSliceImpl& other) = delete;
  // Move constructors. |other| will not hold a reference to the data buffer
  // after this call completes.
  QuicMemSliceImpl(QuicMemSliceImpl&& other) noexcept { *this = std::move(other); }

  QuicMemSliceImpl& operator=(const QuicMemSliceImpl& other) = delete;
  QuicMemSliceImpl& operator=(QuicMemSliceImpl&& other) noexcept {
    if (this != &other) {
      single_slice_buffer_.move(other.single_slice_buffer_);
    }
    return *this;
  }

  // Below methods implements interface needed by QuicMemSlice.
  void Reset() { single_slice_buffer_.drain(length()); }

  // Returns a char pointer to the one and only slice in buffer.
  const char* data() const;

  size_t length() const { return single_slice_buffer_.length(); }
  bool empty() const { return length() == 0; }

private:
  // Used to align both fragment and buffer at max aligned address.
  struct BufferFragmentBundle {
    static BufferFragmentBundle* createBundleWithSize(size_t length) {
      return reinterpret_cast<BufferFragmentBundle*>(
          new char[sizeof(BufferFragmentBundle) + length]);
    }

    // TODO(danzh) fragment_ is not aligned in memory. This can cause extra
    // CPU read when accessing MemSlice::data(). Investigate suggestion
    // in https://github.com/envoyproxy/envoy/pull/6400/files#r277272709 to
    // mitigate the extra cost if it stands out.
    // https://stackoverflow.com/questions/54049474/does-aligning-memory-on-particular-address-boundaries-in-c-c-still-improve-x86/54049733#54049733
    // suggests that aligning on page boundary also benefits performance.
    Envoy::Buffer::BufferFragmentImpl fragment_;
    char buffer_[];
  };

  // Prerequisite: buffer has at least one slice.
  size_t firstSliceLength(Envoy::Buffer::Instance& buffer);

  Envoy::Buffer::OwnedImpl single_slice_buffer_;
};

} // namespace quic
