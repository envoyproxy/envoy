#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "absl/memory/memory.h"

namespace quiche {

template <typename T> std::unique_ptr<T> QuicheWrapUniqueImpl(T* ptr) {
  return absl::WrapUnique<T>(ptr);
}

} // namespace quiche
