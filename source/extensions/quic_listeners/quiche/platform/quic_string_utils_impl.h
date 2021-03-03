#pragma once

#include "absl/strings/str_cat.h"

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quic {

template <typename... Args>
inline void QuicStrAppendImpl(std::string* output, const Args&... args) {
  absl::StrAppend(output, args...);
}

} // namespace quic
