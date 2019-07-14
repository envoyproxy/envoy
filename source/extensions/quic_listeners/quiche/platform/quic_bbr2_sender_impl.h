#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "quiche/quic/core/congestion_control/bbr_sender.h"

namespace quic {

using QuicBbr2SenderImpl = BbrSender;

} // namespace quic
