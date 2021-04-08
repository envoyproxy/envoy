#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "common/quic/platform/quic_logging_impl.h"

// TODO(wub): Implement exponential back off to avoid performance problems due
// to excessive QUIC_BUG.
#define QUICHE_BUG_IMPL(bug_id) QUICHE_LOG_IMPL(DFATAL)
#define QUICHE_BUG_IF_IMPL(bug_id, condition) QUICHE_LOG_IF_IMPL(DFATAL, condition)
#define QUICHE_PEER_BUG_IMPL(bug_id) QUICHE_LOG_IMPL(ERROR)
#define QUICHE_PEER_BUG_IF_IMPL(bug_id, condition) QUICHE_LOG_IF_IMPL(ERROR, condition)
