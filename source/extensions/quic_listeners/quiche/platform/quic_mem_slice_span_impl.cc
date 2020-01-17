// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_mem_slice_span_impl.h"

#include "quiche/quic/platform/api/quic_mem_slice.h"

namespace quic {

quiche::QuicheStringPiece QuicMemSliceSpanImpl::GetData(size_t index) {
  absl::FixedArray<Envoy::Buffer::RawSlice> slices(index + 1);
  uint64_t num_slices = buffer_->getAtMostNRawSlices(slices.begin(), index + 1);
  ASSERT(num_slices > index);
  return {reinterpret_cast<char*>(slices[index].mem_), slices[index].len_};
}

} // namespace quic
