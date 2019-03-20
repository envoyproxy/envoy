#include "extensions/quic_listeners/quiche/platform/quic_mem_slice_storage_impl.h"

namespace quic {

namespace {
QuicMemSliceStorageImpl::QuicMemSliceStorageImpl(
    const struct iovec* iov,
    int iov_count,
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
    void* dst = allocator->New(slice_len);
    QuicUtils::CopyToBuffer(iov, iov_count, io_offset, slice_len, std::static_cast<char*>(dst));
    io_offset += slice_len;
    Envoy::BufferFragmentImpl fragment(dst, slice_len,
            [allocator](const void* data, size_t, const Envoy::Buffer::BufferFragmentImpl*) {
              allocator->Delete(const_cast<char*>(static_cast<const char*>(data)));
            });
    buffer_.addBufferFragment(fragment);
  }
}

}  // namespace

} // namespace quic
