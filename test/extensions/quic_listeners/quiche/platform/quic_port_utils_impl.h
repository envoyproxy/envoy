#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quic {

int QuicPickServerPortForTestsOrDieImpl();
inline void QuicRecyclePortImpl(int) {
  // No-op with current port picking implementation.
}

} // namespace quic
