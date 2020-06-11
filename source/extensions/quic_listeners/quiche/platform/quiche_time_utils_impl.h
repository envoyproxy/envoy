// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#pragma once

#include <cstdint>

#include "extensions/quic_listeners/quiche/platform/quiche_optional_impl.h"

#include "absl/time/civil_time.h"
#include "absl/time/time.h"

namespace quiche {

QuicheOptionalImpl<int64_t> QuicheUtcDateTimeToUnixSecondsImpl(int year, int month, int day,
                                                               int hour, int minute, int second);

} // namespace quiche
