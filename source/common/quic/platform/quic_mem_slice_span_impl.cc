// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "source/common/quic/platform/quic_mem_slice_span_impl.h"

#include "quiche/quic/platform/api/quic_mem_slice.h"

namespace quic {

// NOLINTNEXTLINE(readability-identifier-naming)
absl::string_view QuicMemSliceSpanImpl::GetData(size_t index) {
  Envoy::Buffer::RawSliceVector slices = buffer_->getRawSlices(/*max_slices=*/index + 1);
  ASSERT(slices.size() > index);
  return {reinterpret_cast<char*>(slices[index].mem_), slices[index].len_};
}

} // namespace quic
