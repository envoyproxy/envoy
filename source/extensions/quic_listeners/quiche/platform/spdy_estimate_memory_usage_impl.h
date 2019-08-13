#pragma once

#include <cstddef>

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace spdy {

// Dummy implementation.
template <class T> size_t SpdyEstimateMemoryUsageImpl(const T& /*object*/) { return 0; }

} // namespace spdy
