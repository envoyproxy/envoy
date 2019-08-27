#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "common/buffer/buffer_impl.h"

#include "extensions/quic_listeners/quiche/platform/quic_mem_slice_span_impl.h"

namespace quic {
namespace test {

class QuicTestMemSliceVectorImpl {
public:
  explicit QuicTestMemSliceVectorImpl(std::vector<std::pair<char*, size_t>> buffers) {
    for (auto it : buffers) {
      auto fragment = new Envoy::Buffer::BufferFragmentImpl(
          it.first, it.second,
          [](const void*, size_t, const Envoy::Buffer::BufferFragmentImpl* fragment) {
            delete fragment;
          });
      buffer_.addBufferFragment(*fragment);
    }
  }

  QuicMemSliceSpanImpl span() { return QuicMemSliceSpanImpl(buffer_); }

private:
  Envoy::Buffer::OwnedImpl buffer_;
};

} // namespace test
} // namespace quic
