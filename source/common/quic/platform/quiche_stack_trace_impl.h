#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <sstream>
#include <string>
#include <vector>

#include "source/server/backtrace.h"

#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"

namespace quiche {

// NOLINTNEXTLINE(readability-identifier-naming)
inline std::vector<void*> CurrentStackTraceImpl() {
  constexpr int kMaxStackSize = 64;
  std::vector<void*> stacktrace(kMaxStackSize, nullptr);
  const int depth = absl::GetStackTrace(stacktrace.data(), stacktrace.size(),
                                        /*skip_count=*/0);
  if (depth <= 0) {
    return {};
  }
  stacktrace.resize(depth);
  return stacktrace;
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline std::string SymbolizeStackTraceImpl(absl::Span<void* const> stacktrace) {
  std::ostringstream os;
  for (size_t i = 0; i < stacktrace.size(); ++i) {
    void* const addr = stacktrace[i];
    char out[1024];
    const bool success = absl::Symbolize(addr, out, sizeof(out));
    if (success) {
      os << "#" << i << " " << out << " [" << addr << "]\n";
    } else {
      os << "#" << i << " [" << addr << "]\n";
    }
  }
  return os.str();
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline std::string QuicheStackTraceImpl() {
  Envoy::BackwardsTrace t;
  t.capture();
  std::ostringstream os;
  t.printTrace(os);
  return os.str();
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline bool QuicheShouldRunStackTraceTestImpl() { return true; }

} // namespace quiche
