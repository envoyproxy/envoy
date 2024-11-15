#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "quiche/common/platform/api/quiche_logging.h"

#define QUICHE_BUG_IMPL(b) QUICHE_DLOG(DFATAL) << #b ": "
#define QUICHE_BUG_IF_IMPL(b, condition) QUICHE_DLOG_IF(DFATAL, condition) << #b ": "
#define QUICHE_PEER_BUG_IMPL(b) QUICHE_DLOG(DFATAL) << #b ": "
#define QUICHE_PEER_BUG_IF_IMPL(b, condition) QUICHE_DLOG_IF(DFATAL, condition) << #b ": "
