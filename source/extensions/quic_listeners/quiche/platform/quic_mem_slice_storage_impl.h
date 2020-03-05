#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "common/buffer/buffer_impl.h"

#include "quiche/quic/core/quic_buffer_allocator.h"
#include "quiche/quic/platform/api/quic_iovec.h"
#include "quiche/quic/platform/api/quic_mem_slice_span.h"

namespace quic {

// QuicMemSliceStorageImpl wraps a MemSlice vector.
class QuicMemSliceStorageImpl {
public:
  QuicMemSliceStorageImpl(const iovec* iov, int iov_count, QuicBufferAllocator* allocator,
                          const QuicByteCount max_slice_len);

  QuicMemSliceStorageImpl(const QuicMemSliceStorageImpl& other) { *this = other; }

  QuicMemSliceStorageImpl& operator=(const QuicMemSliceStorageImpl& other) {
    if (this != &other) {
      for (auto& mem_slice : other.mem_slices_) {
        Envoy::Buffer::OwnedImpl buffer;
        buffer.add(mem_slice.data(), mem_slice.length());
        mem_slices_.emplace_back(buffer, mem_slice.length());
      }
    }
    return *this;
  }
  QuicMemSliceStorageImpl(QuicMemSliceStorageImpl&& other) = default;
  QuicMemSliceStorageImpl& operator=(QuicMemSliceStorageImpl&& other) = default;

  QuicMemSliceSpan ToSpan() {
    return QuicMemSliceSpan(QuicMemSliceSpanImpl(absl::Span<quic::QuicMemSliceImpl>(mem_slices_)));
  }

  void Append(QuicMemSliceImpl mem_slice) { mem_slices_.push_back(std::move(mem_slice)); }

private:
  std::vector<quic::QuicMemSliceImpl> mem_slices_;
};

} // namespace quic
