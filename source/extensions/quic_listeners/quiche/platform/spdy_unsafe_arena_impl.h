#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "quiche/spdy/core/spdy_simple_arena.h"

namespace spdy {

using SpdyUnsafeArenaImpl = SpdySimpleArena;

} // namespace spdy
