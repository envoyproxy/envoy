#pragma once

#include "absl/base/optimization.h"

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#define QUIC_ALIGN_OF_IMPL alignof
#ifdef _MSC_VER
#define QUIC_ALIGNED_IMPL(X) __declspec(align(X))
#else
#define QUIC_ALIGNED_IMPL(X) __attribute__((aligned(X)))
#endif
#define QUIC_CACHELINE_ALIGNED_IMPL ABSL_CACHELINE_ALIGNED
#define QUIC_CACHELINE_SIZE_IMPL ABSL_CACHELINE_SIZE
