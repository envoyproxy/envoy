#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "server/backtrace.h"

#include "quiche/quic/platform/api/quic_string.h"

namespace quic {

inline QuicString QuicStackTraceImpl() {
  Envoy::BackwardsTrace t;
  t.capture();
  std::ostringstream os;
  t.printTrace(os);
  return os.str();
}

} // namespace quic
