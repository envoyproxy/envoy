#pragma once

#include "absl/types/optional.h"

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quiche {

template <typename T> using QuicheOptionalImpl = absl::optional<T>;

#define QuicheNullOptImpl absl::nullopt

} // namespace quiche
