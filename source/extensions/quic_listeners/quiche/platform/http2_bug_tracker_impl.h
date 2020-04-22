#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_bug_tracker_impl.h"

#define HTTP2_BUG_IMPL QUIC_BUG_IMPL
#define HTTP2_BUG_IF_IMPL QUIC_BUG_IF_IMPL
#define FLAGS_http2_always_log_bugs_for_tests_IMPL true
