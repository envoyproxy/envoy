#pragma once

#include <deque>

// NOLINT(namespace-envoy)

namespace http2 {

template <typename T> using Http2DequeImpl = std::deque<T>;

} // namespace http2
