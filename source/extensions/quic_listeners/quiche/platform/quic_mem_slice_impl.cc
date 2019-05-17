// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_mem_slice_impl.h"

#include "common/common/assert.h"

namespace quic {

Envoy::Buffer::BufferFragmentImpl*
QuicMemSliceImpl::allocateBufferAndFragment(QuicBufferAllocator* /*allocator*/, size_t length) {
  BufferFragmentBundle* bundle = BufferFragmentBundle::createBundleWithSize(length);
  Envoy::Buffer::BufferFragmentImpl& fragment = bundle->fragment_;
  return new (&fragment) Envoy::Buffer::BufferFragmentImpl(
      bundle->buffer_, length,
      [](const void*, size_t, const Envoy::Buffer::BufferFragmentImpl* frag) {
        // Delete will deallocate the bundle of fragment and buffer.
        delete[] const_cast<char*>(reinterpret_cast<const char*>(frag));
      });
}

QuicMemSliceImpl::QuicMemSliceImpl(QuicBufferAllocator* allocator, size_t length) {
  Envoy::Buffer::BufferFragmentImpl* fragment = allocateBufferAndFragment(allocator, length);
  single_slice_buffer_.addBufferFragment(*fragment);
}

QuicMemSliceImpl::QuicMemSliceImpl(Envoy::Buffer::Instance& buffer, size_t length) {
  ASSERT(firstSliceLength(buffer) == length);
  single_slice_buffer_.move(buffer, length);
  ASSERT(single_slice_buffer_.getRawSlices(nullptr, 0) == 1);
}

const char* QuicMemSliceImpl::data() const {
  Envoy::Buffer::RawSlice out;
  uint64_t num_slices = single_slice_buffer_.getRawSlices(&out, 1);
  ASSERT(num_slices <= 1);
  return static_cast<const char*>(out.mem_);
}

size_t QuicMemSliceImpl::firstSliceLength(Envoy::Buffer::Instance& buffer) {
  Envoy::Buffer::RawSlice slice;
  uint64_t total_num = buffer.getRawSlices(&slice, 1);
  ASSERT(total_num != 0);
  return slice.len_;
}

} // namespace quic
