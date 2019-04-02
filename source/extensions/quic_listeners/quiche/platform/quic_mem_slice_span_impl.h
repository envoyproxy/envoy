#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "envoy/buffer/buffer.h"

#include "common/common/stack_array.h"

#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_mem_slice.h"
#include "quiche/quic/platform/api/quic_string_piece.h"

namespace quic {

// Wrap a Buffer::Instance and deliver its data to quic stream through as less
// copy as possible.
class QuicMemSliceSpanImpl {
public:
  /**
   * @param buffer has to outlive the life time of this class.
   */
  explicit QuicMemSliceSpanImpl(Envoy::Buffer::Instance& buffer) : buffer_(buffer) {}

  QuicMemSliceSpanImpl(const QuicMemSliceSpanImpl& other) = default;
  QuicMemSliceSpanImpl& operator=(const QuicMemSliceSpanImpl& other) = default;
  QuicMemSliceSpanImpl(QuicMemSliceSpanImpl&& other) = default;
  QuicMemSliceSpanImpl& operator=(QuicMemSliceSpanImpl&& other) = default;

  QuicStringPiece GetData(size_t index) {
    uint64_t num_slices = buffer_.getRawSlices(nullptr, 0);
    ASSERT(num_slices > index);
    Envoy::STACK_ARRAY(slices, Envoy::Buffer::RawSlice, num_slices);
    buffer_.getRawSlices(slices.begin(), num_slices);
    return QuicStringPiece(reinterpret_cast<char*>(slices[index].mem_), slices[index].len_);
  }

  QuicByteCount total_length() { return buffer_.length(); };

  size_t NumSlices() { return buffer_.getRawSlices(nullptr, 0); }

  template <typename ConsumeFunction> QuicByteCount ConsumeAll(ConsumeFunction consume) {
    uint64_t num_slices = buffer_.getRawSlices(nullptr, 0);
    Envoy::STACK_ARRAY(slices, Envoy::Buffer::RawSlice, num_slices);
    buffer_.getRawSlices(slices.begin(), num_slices);
    size_t saved_length = 0;
    for (auto slice : slices) {
      if (slice.len_ == 0) {
        continue;
      }
      Envoy::Buffer::OwnedImpl single_slice_buffer;
      // Move each slice into a stand-alone buffer.
      // TODO(danzh): investigate the cost of allocating one buffer per slice.
      // If it turns out to be expensive, add a new function to free data in the middle in buffer
      // interface and re-design QuicMemSliceImpl.
      single_slice_buffer.move(buffer_, slice.len_);
      consume(QuicMemSlice(QuicMemSliceImpl(single_slice_buffer)));
      saved_length += slice.len_;
    }
    return saved_length;
  }

  bool empty() const { return buffer_.length() == 0; }

private:
  Envoy::Buffer::Instance& buffer_;
};

} // namespace quic
