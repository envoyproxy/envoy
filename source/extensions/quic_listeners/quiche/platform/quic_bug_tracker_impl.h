#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "quiche/quic/platform/api/quic_logging.h"

#define QUIC_BUG_IMPL QUIC_LOG(DFATAL)
#define QUIC_BUG_IF_IMPL(condition) QUIC_LOG_IF(DFATAL, condition)
#define QUIC_PEER_BUG_IMPL QUIC_LOG(ERROR)
#define QUIC_PEER_BUG_IF_IMPL(condition) QUIC_LOG_IF(ERROR, condition)
