#pragma once

#include "absl/types/optional.h"

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace http2 {

template <typename T> using Http2OptionalImpl = absl::optional<T>;

} // namespace http2
