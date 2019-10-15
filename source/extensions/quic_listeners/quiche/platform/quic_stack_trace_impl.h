#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <sstream>
#include <string>

#include "server/backtrace.h"

namespace quic {

inline std::string QuicStackTraceImpl() {
  Envoy::BackwardsTrace t;
  t.capture();
  std::ostringstream os;
  t.printTrace(os);
  return os.str();
}

} // namespace quic
