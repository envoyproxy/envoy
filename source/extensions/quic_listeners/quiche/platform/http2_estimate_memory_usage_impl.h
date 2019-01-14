#pragma once

#include <cstddef>

// NOLINT(namespace-envoy)

namespace http2 {

template <class T> size_t Http2EstimateMemoryUsageImpl(const T& /*object*/) { return 0; }

} // namespace http2
