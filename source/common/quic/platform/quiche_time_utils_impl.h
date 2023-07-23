#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <cstdint>

#include "absl/types/optional.h"

namespace quiche {

// NOLINTNEXTLINE(readability-identifier-naming)
absl::optional<int64_t> QuicheUtcDateTimeToUnixSecondsImpl(int year, int month, int day, int hour,
                                                           int minute, int second);

} // namespace quiche
