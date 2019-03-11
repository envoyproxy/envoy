#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/flags_impl.h"

#define GetHttp2ReloadableFlagImpl(flag) quiche::FLAGS_http2_reloadable_flag_##flag.getValue()
#define SetHttp2ReloadableFlagImpl(flag, value)                                                    \
  quiche::FLAGS_http2_reloadable_flag_##flag.SetValue(value)
