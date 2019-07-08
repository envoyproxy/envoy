#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "quiche/quic/platform/api/quic_string_piece.h"

namespace quic {

void QuicRecordTestOutputImpl(QuicStringPiece identifier, QuicStringPiece data);

} // namespace quic
