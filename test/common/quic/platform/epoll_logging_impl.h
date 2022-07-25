#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "quiche_platform_impl/quiche_logging_impl.h"

namespace epoll_server {

#define CHECK(expression) QUICHE_CHECK_IMPL(expression)
#define CHECK_EQ(a, b) QUICHE_CHECK_EQ_IMPL(a, b)
#define CHECK_NE(a, b) QUICHE_CHECK_NE_IMPL(a, b)
#define DCHECK(expression) QUICHE_DCHECK_IMPL(expression)
#define DCHECK_GE(a, b) QUICHE_DCHECK_GE_IMPL(a, b)
#define DCHECK_GT(a, b) QUICHE_DCHECK_GT_IMPL(a, b)
#define DCHECK_NE(a, b) QUICHE_DCHECK_NE_IMPL(a, b)
#define DCHECK_EQ(a, b) QUICHE_DCHECK_EQ_IMPL(a, b)

#define EPOLL_LOG_IMPL(severity) QUICHE_LOG_IMPL(severity)
#define EPOLL_VLOG_IMPL(verbosity) QUICHE_VLOG_IMPL(verbosity)

#define EPOLL_PLOG_IMPL(severity) QUICHE_PLOG_IMPL(severity)

#define EPOLL_DVLOG_IMPL(verbosity) QUICHE_DVLOG_IMPL(verbosity)

} // namespace epoll_server
