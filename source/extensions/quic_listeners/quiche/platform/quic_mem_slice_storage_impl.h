#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "common/buffer/buffer_impl.h"

#include "quiche/quic/platform/api/quic_iovec.h"
#include "quiche/quic/platform/api/quic_mem_slice_span.h"

namespace quic {

// QuicMemSliceStorageImpl wraps a MemSlice vector.
class QuicMemSliceStorageImpl {
public:
  QuicMemSliceStorageImpl(const struct iovec* iov, int iov_count, QuicBufferAllocator* allocator,
                          const QuicByteCount max_slice_len);

  QuicMemSliceStorageImpl(const QuicMemSliceStorageImpl& other) = default;
  QuicMemSliceStorageImpl& operator=(const QuicMemSliceStorageImpl& other) = default;
  QuicMemSliceStorageImpl(QuicMemSliceStorageImpl&& other) = default;
  QuicMemSliceStorageImpl& operator=(QuicMemSliceStorageImpl&& other) = default;

  QuicMemSliceSpan ToSpan() { return QuicMemSliceSpan(QuicMemSliceSpanImpl(buffer_)); }

private:
  Envoy::Buffer::OwnedImpl buffer_;
};

} // namespace quic
