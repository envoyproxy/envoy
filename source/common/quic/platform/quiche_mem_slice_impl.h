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
#include "quiche/common/quiche_callbacks.h"

namespace quiche {

// Implements the interface required by
// https://github.com/google/quiche/blob/main/common/platform/api/quiche_mem_slice.h
class QuicheMemSliceImpl {
public:
  // Constructs an empty QuicheMemSliceImpl.
  QuicheMemSliceImpl() = default;

  ~QuicheMemSliceImpl();

  // Constructs a QuicheMemSliceImpl by taking ownership of the memory in `buffer`.
  QuicheMemSliceImpl(quiche::QuicheBuffer buffer);
  QuicheMemSliceImpl(std::unique_ptr<char[]> buffer, size_t length);
  QuicheMemSliceImpl(const char* buffer, size_t length, SingleUseCallback<void(const char*)>);

  // Constructs a QuicheMemSliceImpl and moves the first slice of `buffer` into
  // it. Prerequisite: `buffer` must be non-empty, and its first slice must
  // have length `length`.
  explicit QuicheMemSliceImpl(Envoy::Buffer::Instance& buffer, size_t length);

  QuicheMemSliceImpl(const QuicheMemSliceImpl& other) = delete;
  // Move constructors. `other` will not hold a reference to the data buffer
  // after this call completes.
  QuicheMemSliceImpl(QuicheMemSliceImpl&& other) noexcept { *this = std::move(other); }

  QuicheMemSliceImpl& operator=(const QuicheMemSliceImpl& other) = delete;
  QuicheMemSliceImpl& operator=(QuicheMemSliceImpl&& other) noexcept {
    if (this != &other) {
      // OwnedImpl::move() appends data from `other` without clearing the buffer
      // first. It is necessary to call Reset() beforehand to clear
      // `single_slice_buffer_`.
      Reset();
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

private:
  // Owns data of this buffer if `single_slice_buffer_` has a slice that does
  // not own data. `nullptr` otherwise.
  std::unique_ptr<Envoy::Buffer::BufferFragmentImpl> fragment_;

  // A buffer that holds zero or one slice.
  // The slice might or might not own its data.
  Envoy::Buffer::OwnedImpl single_slice_buffer_;
};

} // namespace quiche
