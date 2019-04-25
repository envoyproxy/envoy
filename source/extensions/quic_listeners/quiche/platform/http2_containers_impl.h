#pragma once

#include <deque>

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace http2 {

template <typename T> using Http2DequeImpl = std::deque<T>;

} // namespace http2
