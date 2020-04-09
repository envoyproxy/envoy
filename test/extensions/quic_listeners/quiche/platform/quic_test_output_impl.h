#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "quiche/common/platform/api/quiche_string_piece.h"

namespace quic {

void QuicSaveTestOutputImpl(quiche::QuicheStringPiece filename, quiche::QuicheStringPiece data);

bool QuicLoadTestOutputImpl(quiche::QuicheStringPiece filename, std::string* data);

void QuicRecordTraceImpl(quiche::QuicheStringPiece identifier, quiche::QuicheStringPiece data);

} // namespace quic
