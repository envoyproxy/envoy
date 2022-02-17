// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "source/common/quic/platform/quic_mem_slice_impl.h"

#include "envoy/buffer/buffer.h"

#include "source/common/common/assert.h"

namespace quic {

QuicMemSliceImpl::QuicMemSliceImpl(QuicUniqueBufferPtr buffer, size_t length)
    : fragment_(std::make_unique<Envoy::Buffer::BufferFragmentImpl>(
          buffer.get(), length,
          // TODO(danzh) change the buffer fragment constructor to take the lambda by move instead
          // of copy, so that the ownership of |buffer| can be transferred to lambda via capture
          // here and below to unify and simplify the constructor implementations.
          [allocator = buffer.get_deleter().allocator()](const void* p, size_t,
                                                         const Envoy::Buffer::BufferFragmentImpl*) {
            quic::QuicBufferDeleter deleter(allocator);
            deleter(const_cast<char*>(static_cast<const char*>(p)));
          })) {
  buffer.release();
  single_slice_buffer_.addBufferFragment(*fragment_);
  ASSERT(this->length() == length);
}

QuicMemSliceImpl::QuicMemSliceImpl(Envoy::Buffer::Instance& buffer, size_t length) {
  ASSERT(firstSliceLength(buffer) == length);
  single_slice_buffer_.move(buffer, length);
  ASSERT(single_slice_buffer_.getRawSlices().size() == 1);
}

QuicMemSliceImpl::QuicMemSliceImpl(std::unique_ptr<char[]> buffer, size_t length)
    : fragment_(std::make_unique<Envoy::Buffer::BufferFragmentImpl>(
          buffer.release(), length,
          [](const void* p, size_t, const Envoy::Buffer::BufferFragmentImpl*) {
            delete[] static_cast<const char*>(p);
          })) {
  single_slice_buffer_.addBufferFragment(*fragment_);
  ASSERT(this->length() == length);
}

QuicMemSliceImpl::~QuicMemSliceImpl() {
  ASSERT(fragment_ == nullptr || (firstSliceLength(single_slice_buffer_) == fragment_->size() &&
                                  data() == fragment_->data()));
}

const char* QuicMemSliceImpl::data() const {
  return reinterpret_cast<const char*>(single_slice_buffer_.frontSlice().mem_);
}

size_t QuicMemSliceImpl::firstSliceLength(Envoy::Buffer::Instance& buffer) {
  return buffer.frontSlice().len_;
}

} // namespace quic
