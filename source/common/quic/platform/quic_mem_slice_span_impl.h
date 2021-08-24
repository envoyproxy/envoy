#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "envoy/buffer/buffer.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_mem_slice.h"

namespace quic {

// Implements the interface required by
// https://quiche.googlesource.com/quiche/+/refs/heads/master/quic/platform/api/quic_mem_slice_span.h
// Wraps a Buffer::Instance and deliver its data with minimum number of copies.
class QuicMemSliceSpanImpl {
public:
  QuicMemSliceSpanImpl() = default;
  /**
   * @param buffer has to outlive the life time of this class.
   */
  explicit QuicMemSliceSpanImpl(Envoy::Buffer::Instance& buffer) : buffer_(&buffer) {}
  explicit QuicMemSliceSpanImpl(QuicMemSliceImpl* slice)
      : buffer_(&slice->getSingleSliceBuffer()), mem_slice_(slice) {}

  QuicMemSliceSpanImpl(const QuicMemSliceSpanImpl& other) = default;
  QuicMemSliceSpanImpl& operator=(const QuicMemSliceSpanImpl& other) = default;

  QuicMemSliceSpanImpl(QuicMemSliceSpanImpl&& other) noexcept
      : buffer_(other.buffer_), mem_slice_(other.mem_slice_) {
    other.buffer_ = nullptr;
    other.mem_slice_ = nullptr;
  }

  QuicMemSliceSpanImpl& operator=(QuicMemSliceSpanImpl&& other) noexcept {
    if (this != &other) {
      buffer_ = other.buffer_;
      mem_slice_ = other.mem_slice_;
      other.buffer_ = nullptr;
      other.mem_slice_ = nullptr;
    }
    return *this;
  }

  // QuicMemSliceSpan
  // NOLINTNEXTLINE(readability-identifier-naming)
  absl::string_view GetData(size_t index);
  // NOLINTNEXTLINE(readability-identifier-naming)
  QuicByteCount total_length() { return buffer_->length(); };
  // NOLINTNEXTLINE(readability-identifier-naming)
  size_t NumSlices() { return buffer_->getRawSlices().size(); }
  // NOLINTNEXTLINE(readability-identifier-naming)
  template <typename ConsumeFunction> QuicByteCount ConsumeAll(ConsumeFunction consume);
  bool empty() const { return buffer_->length() == 0; }

private:
  // If constructed with a QuicMemSlice, mem_slice_ point to that object and this points to
  // mem_slice_->getSingleSliceBuffer(). If constructed with an Envoy buffer, this points to the
  // buffer itself.
  Envoy::Buffer::Instance* buffer_{nullptr};
  // If this span is not constructed with a QuicMemSlice, this points to nullptr.
  QuicMemSliceImpl* mem_slice_{nullptr};
};

template <typename ConsumeFunction>
// NOLINTNEXTLINE(readability-identifier-naming)
QuicByteCount QuicMemSliceSpanImpl::ConsumeAll(ConsumeFunction consume) {
  size_t saved_length = 0;
  if (mem_slice_ == nullptr) {
    for (auto& slice : buffer_->getRawSlices()) {
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
  } else {
    saved_length += mem_slice_->length();
    consume(quic::QuicMemSlice(std::move(*mem_slice_)));
  }
  ASSERT(buffer_->length() == 0);
  return saved_length;
}

} // namespace quic
