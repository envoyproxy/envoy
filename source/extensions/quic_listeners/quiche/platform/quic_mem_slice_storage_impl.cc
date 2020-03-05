// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_mem_slice_storage_impl.h"

#include <cstdint>

#include "envoy/buffer/buffer.h"

#include "quiche/quic/core/quic_utils.h"

namespace quic {

// TODO(danzh)Note that |allocator| is not used to allocate memory currently, instead,
// Buffer::OwnedImpl allocates memory on its own. Investigate if a customized
// QuicBufferAllocator can improve cache hit.
QuicMemSliceStorageImpl::QuicMemSliceStorageImpl(const iovec* iov, int iov_count,
                                                 QuicBufferAllocator* allocator,
                                                 const QuicByteCount max_slice_len) {
  if (iov == nullptr) {
    return;
  }
  QuicByteCount write_len = 0;
  for (int i = 0; i < iov_count; ++i) {
    write_len += iov[i].iov_len;
  }
  size_t io_offset = 0;
  while (io_offset < write_len) {
    size_t slice_len = std::min(write_len - io_offset, max_slice_len);
    quic::QuicUniqueBufferPtr buffer = quic::MakeUniqueBuffer(allocator, slice_len);
    QuicUtils::CopyToBuffer(iov, iov_count, io_offset, slice_len, buffer.get());
    mem_slices_.emplace_back(std::move(buffer), slice_len);
    io_offset += slice_len;
  }
}

} // namespace quic
