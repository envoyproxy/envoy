#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_logging_impl.h"

namespace epoll_server {

#define EPOLL_LOG_IMPL(severity) QUIC_LOG_IMPL(severity)
#define EPOLL_VLOG_IMPL(verbosity) QUIC_VLOG_IMPL(verbosity)

#define EPOLL_PLOG_IMPL(severity) QUIC_PLOG_IMPL(severity)

#ifndef NDEBUG
#define EPOLL_DVLOG_IMPL(verbosity) QUIC_VLOG_IMPL(verbosity)
#else
#define EPOLL_DVLOG_IMPL(verbosity) QUIC_VLOG_IF_IMPL(verbosity, false)
#endif

} // namespace epoll_server
