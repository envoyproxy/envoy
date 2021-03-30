#pragma once

#include "absl/strings/str_cat.h"
#include "fmt/printf.h"

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quiche {

template <typename... Args> inline std::string QuicheStrCatImpl(const Args&... args) {
  return absl::StrCat(args...);
}

template <typename... Args> inline std::string QuicheStringPrintfImpl(const Args&... args) {
  return fmt::sprintf(std::forward<const Args&>(args)...);
}

} // namespace quiche
