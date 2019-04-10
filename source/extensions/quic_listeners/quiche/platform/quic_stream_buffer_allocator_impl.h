#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "quiche/quic/core/quic_buffer_allocator.h"

namespace quic {

// Implements the interface required by
// https://quiche.googlesource.com/quiche/+/refs/heads/master/quic/platform/api/quic_stream_buffer_allocator.h
class QuicStreamBufferAllocatorImpl : public QuicBufferAllocator {
public:
  ~QuicStreamBufferAllocatorImpl() override {}

  char* New(size_t size) override { return static_cast<char*>(malloc(size)); }

  char* New(size_t size, bool) override { return New(size); }

  void Delete(char* buf) override { free(buf); }
};

} // namespace quic
