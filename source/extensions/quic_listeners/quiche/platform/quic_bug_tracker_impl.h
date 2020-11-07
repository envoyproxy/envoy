#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_logging_impl.h"

// TODO(wub): Implement exponential back off to avoid performance problems due
// to excessive QUIC_BUG.
#define QUIC_BUG_IMPL QUICHE_LOG_IMPL(DFATAL)
#define QUIC_BUG_IF_IMPL(condition) QUICHE_LOG_IF_IMPL(DFATAL, condition)
#define QUIC_PEER_BUG_IMPL QUICHE_LOG_IMPL(ERROR)
#define QUIC_PEER_BUG_IF_IMPL(condition) QUICHE_LOG_IF_IMPL(ERROR, condition)
