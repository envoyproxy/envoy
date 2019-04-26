#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_flags_impl.h"

// Needed till direct use of FLAGS_quic_supports_tls_handshake is replaced by
// GetQuicFlag() in QUICHE.
bool FLAGS_quic_supports_tls_handshake = false;
