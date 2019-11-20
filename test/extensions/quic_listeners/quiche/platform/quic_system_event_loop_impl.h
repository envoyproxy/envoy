#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <string>

inline void QuicRunSystemEventLoopIterationImpl() {
  // No-op.
}

class QuicSystemEventLoopImpl {
public:
  // Only used by quic_client_bin.cc which is not required in Envoy.
  QuicSystemEventLoopImpl(std::string /*context_name*/) {}
};
