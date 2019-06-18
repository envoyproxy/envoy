#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <utility>

#include "extensions/quic_listeners/quiche/platform/quic_logging_impl.h"

#include "absl/base/macros.h"

#define HTTP2_FALLTHROUGH_IMPL ABSL_FALLTHROUGH_INTENDED
#define HTTP2_DIE_IF_NULL_IMPL(ptr) dieIfNull(ptr)
#define HTTP2_UNREACHABLE_IMPL() DCHECK(false)

namespace http2 {

template <typename T> inline T dieIfNull(T&& ptr) {
  CHECK((ptr) != nullptr);
  return std::forward<T>(ptr);
}

} // namespace http2
