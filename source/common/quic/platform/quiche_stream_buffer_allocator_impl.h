#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "quiche/common/simple_buffer_allocator.h"

namespace quiche {

// Implements the interface required by
// https://quiche.googlesource.com/quiche/+/refs/heads/master/quic/platform/api/quic_stream_buffer_allocator.h
// with the default implementation provided by QUICHE.
using QuicheStreamBufferAllocatorImpl = quiche::SimpleBufferAllocator;

} // namespace quiche
