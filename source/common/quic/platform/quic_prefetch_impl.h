#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quic {

#if defined(__GNUC__)
// See __builtin_prefetch in:
// https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html.
inline void QuicPrefetchT0Impl(const void* addr) { __builtin_prefetch(addr, 0, 3); }
#else
inline void QuicPrefetchT0Impl(const void*) {}
#endif

} // namespace quic
