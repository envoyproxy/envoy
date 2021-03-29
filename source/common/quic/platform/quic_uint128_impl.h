#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "absl/numeric/int128.h"

namespace quic {

using QuicUint128Impl = absl::uint128;
#define MakeQuicUint128Impl(hi, lo) absl::MakeUint128(hi, lo)
#define QuicUint128Low64Impl(x) absl::Uint128Low64(x)
#define QuicUint128High64Impl(x) absl::Uint128High64(x)

} // namespace quic
