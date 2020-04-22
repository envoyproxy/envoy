#pragma once

#include <cstdint>

#include "envoy/common/platform.h"

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace spdy {

inline uint16_t SpdyNetToHost16Impl(uint16_t x) { return ntohs(x); }

inline uint32_t SpdyNetToHost32Impl(uint32_t x) { return ntohl(x); }

// TODO: implement
inline uint64_t SpdyNetToHost64Impl(uint64_t /*x*/) { return 0; }

inline uint16_t SpdyHostToNet16Impl(uint16_t x) { return htons(x); }

inline uint32_t SpdyHostToNet32Impl(uint32_t x) { return htonl(x); }

// TODO: implement
inline uint64_t SpdyHostToNet64Impl(uint64_t /*x*/) { return 0; }

} // namespace spdy
