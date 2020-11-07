#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/flags_impl.h"

#define GetSpdyReloadableFlagImpl(flag) quiche::FLAGS_spdy_reloadable_flag_##flag->value()

#define GetSpdyRestartFlagImpl(flag) quiche::FLAGS_spdy_restart_flag_##flag->value()

#define SPDY_CODE_COUNT_N_IMPL(flag, instance, total)                                              \
  do {                                                                                             \
  } while (0)

#define SPDY_CODE_COUNT_IMPL(name)                                                                 \
  do {                                                                                             \
  } while (0)
