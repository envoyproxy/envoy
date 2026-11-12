#pragma once

#include <unistd.h>

#include <algorithm>

#include "absl/strings/str_format.h"

namespace Envoy {

/**
 * Format into a buffer on the stack and write it to stderr with a single write(2), for use where
 * neither the logger nor C++ streams can be relied on, in particular in a fatal signal handler.
 * Output that does not fit in the buffer is truncated.
 */
template <typename... Args>
void writeStderr(const absl::FormatSpec<Args...>& format, const Args&... args) {
  // Large enough for a symbol name (see `BackwardsTrace`) or a path, with its decoration.
  char buffer[4608];
  const int size = absl::SNPrintF(buffer, sizeof(buffer), format, args...);
  if (size > 0) {
    // There is nowhere to report a failed write.
    [[maybe_unused]] const ssize_t rc =
        ::write(STDERR_FILENO, buffer, std::min<size_t>(size, sizeof(buffer) - 1));
  }
}

} // namespace Envoy
