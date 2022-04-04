#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <cstddef>
#include <iostream>
#include <memory>

#include "source/common/buffer/buffer_impl.h"

#include "quiche/common/quiche_buffer_allocator.h"

namespace quiche {

// Implements the interface required by
// https://github.com/google/quiche/blob/main/common/platform/api/quiche_mem_slice.h
class QuicheMemSliceImpl {
public:
  // Constructs an empty QuicheMemSliceImpl.
  QuicheMemSliceImpl() = default;

  ~QuicheMemSliceImpl();

  // Constructs a QuicheMemSliceImpl by taking ownership of the memory in |buffer|.
  QuicheMemSliceImpl(quiche::QuicheBuffer buffer);
  QuicheMemSliceImpl(std::unique_ptr<char[]> buffer, size_t length);

  // Constructs a QuicheMemSliceImpl from a Buffer::Instance with first |length| bytes in it.
  // Data will be moved from |buffer| to this mem slice.
  // Prerequisite: |buffer| has at least |length| bytes of data and not empty.
  explicit QuicheMemSliceImpl(Envoy::Buffer::Instance& buffer, size_t length);

  QuicheMemSliceImpl(const QuicheMemSliceImpl& other) = delete;
  // Move constructors. |other| will not hold a reference to the data buffer
  // after this call completes.
  QuicheMemSliceImpl(QuicheMemSliceImpl&& other) noexcept { *this = std::move(other); }

  QuicheMemSliceImpl& operator=(const QuicheMemSliceImpl& other) = delete;
  QuicheMemSliceImpl& operator=(QuicheMemSliceImpl&& other) noexcept {
    if (this != &other) {
      fragment_ = std::move(other.fragment_);
      single_slice_buffer_.move(other.single_slice_buffer_);
    }
    return *this;
  }

  // Below methods implements interface needed by QuicheMemSlice.
  // NOLINTNEXTLINE(readability-identifier-naming)
  void Reset() {
    single_slice_buffer_.drain(length());
    fragment_ = nullptr;
  }

  // Returns a char pointer to the one and only slice in buffer.
  const char* data() const;

  size_t length() const { return single_slice_buffer_.length(); }
  bool empty() const { return length() == 0; }

  Envoy::Buffer::OwnedImpl& getSingleSliceBuffer() { return single_slice_buffer_; }

private:
  size_t firstSliceLength(Envoy::Buffer::Instance& buffer);

  std::unique_ptr<Envoy::Buffer::BufferFragmentImpl> fragment_;
  Envoy::Buffer::OwnedImpl single_slice_buffer_;
};

} // namespace quiche
