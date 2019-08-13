#pragma once

#include "absl/strings/string_view.h"

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quic {

using QuicStringPieceImpl = absl::string_view;

} // namespace quic
