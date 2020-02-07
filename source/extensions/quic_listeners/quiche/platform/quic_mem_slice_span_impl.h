#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "envoy/buffer/buffer.h"

#include "absl/container/fixed_array.h"
#include "quiche/common/platform/api/quiche_string_piece.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_mem_slice.h"

namespace quic {

// Implements the interface required by
// https://quiche.googlesource.com/quiche/+/refs/heads/master/quic/platform/api/quic_mem_slice_span.h
// Wraps a Buffer::Instance and deliver its data with minimum number of copies.
class QuicMemSliceSpanImpl {
public:
  QuicMemSliceSpanImpl() : buffer_(nullptr) {}
  /**
   * @param buffer has to outlive the life time of this class.
   */
  explicit QuicMemSliceSpanImpl(Envoy::Buffer::Instance& buffer) : buffer_(&buffer) {}
  explicit QuicMemSliceSpanImpl(QuicMemSliceImpl* slice) : buffer_(&slice->single_slice_buffer()) {}

  QuicMemSliceSpanImpl(const QuicMemSliceSpanImpl& other) = default;
  QuicMemSliceSpanImpl& operator=(const QuicMemSliceSpanImpl& other) = default;

  QuicMemSliceSpanImpl(QuicMemSliceSpanImpl&& other) noexcept : buffer_(other.buffer_) {
    other.buffer_ = nullptr;
  }

  QuicMemSliceSpanImpl& operator=(QuicMemSliceSpanImpl&& other) noexcept {
    if (this != &other) {
      buffer_ = other.buffer_;
      other.buffer_ = nullptr;
    }
    return *this;
  }

  // QuicMemSliceSpan
  quiche::QuicheStringPiece GetData(size_t index);
  QuicByteCount total_length() { return buffer_->length(); };
  size_t NumSlices() { return buffer_->getRawSlices(nullptr, 0); }
  template <typename ConsumeFunction> QuicByteCount ConsumeAll(ConsumeFunction consume);
  bool empty() const { return buffer_->length() == 0; }

private:
  Envoy::Buffer::Instance* buffer_{nullptr};
};

template <typename ConsumeFunction>
QuicByteCount QuicMemSliceSpanImpl::ConsumeAll(ConsumeFunction consume) {
  uint64_t num_slices = buffer_->getRawSlices(nullptr, 0);
  absl::FixedArray<Envoy::Buffer::RawSlice> slices(num_slices);
  buffer_->getRawSlices(slices.begin(), num_slices);
  size_t saved_length = 0;
  for (auto& slice : slices) {
    if (slice.len_ == 0) {
      continue;
    }
    // Move each slice into a stand-alone buffer.
    // TODO(danzh): investigate the cost of allocating one buffer per slice.
    // If it turns out to be expensive, add a new function to free data in the middle in buffer
    // interface and re-design QuicMemSliceImpl.
    consume(QuicMemSlice(QuicMemSliceImpl(*buffer_, slice.len_)));
    saved_length += slice.len_;
  }
  ASSERT(buffer_->length() == 0);
  return saved_length;
}

} // namespace quic
