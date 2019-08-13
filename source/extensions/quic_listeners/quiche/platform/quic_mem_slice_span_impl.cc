// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_mem_slice_span_impl.h"

#include "quiche/quic/platform/api/quic_mem_slice.h"

namespace quic {

QuicStringPiece QuicMemSliceSpanImpl::GetData(size_t index) {
  uint64_t num_slices = buffer_->getRawSlices(nullptr, 0);
  ASSERT(num_slices > index);
  Envoy::STACK_ARRAY(slices, Envoy::Buffer::RawSlice, num_slices);
  buffer_->getRawSlices(slices.begin(), num_slices);
  return {reinterpret_cast<char*>(slices[index].mem_), slices[index].len_};
}

} // namespace quic
