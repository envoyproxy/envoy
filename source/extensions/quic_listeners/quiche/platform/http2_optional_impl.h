#pragma once

#include "absl/types/optional.h"

// NOLINT(namespace-envoy)

namespace http2 {

template <typename T> using Http2OptionalImpl = absl::optional<T>;

} // namespace http2
