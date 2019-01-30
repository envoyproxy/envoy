#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace quic {

inline void QuicSleepImpl(QuicTime::Delta duration) {
  absl::SleepFor(absl::Microseconds(duration.ToMicroseconds()));
}

} // namespace quic
