#pragma once

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>

#include "absl/strings/str_format.h"

namespace Envoy {

/**
 * Format into a buffer on the stack and write it to stderr with write(2), for use where
 * neither the logger nor C++ streams can be relied on, in particular in a fatal signal handler.
 * Output that does not fit in the buffer is truncated.
 */
template <typename... Args>
void writeStderr(const absl::FormatSpec<Args...>& format, const Args&... args) {
  // Large enough for a symbol name (see `BackwardsTrace`) or a path, with its decoration.
  char buffer[4608];
  const int size = absl::SNPrintF(buffer, sizeof(buffer), format, args...);
  const char* data = buffer;
  size_t remaining = size > 0 ? std::min<size_t>(size, sizeof(buffer) - 1) : 0;
  // Short and interrupted writes are continued. There is nowhere to report any other failure.
  while (remaining > 0) {
    const ssize_t rc = ::write(STDERR_FILENO, data, remaining);
    if (rc > 0) {
      data += rc;
      remaining -= static_cast<size_t>(rc);
    } else if (rc == 0 || errno != EINTR) {
      break;
    }
  }
}

/**
 * Write a backtrace frame to stderr with `writeStderr()`.
 */
inline void writeStderrFrame(int index, const char* symbol, const void* address) {
  if (symbol != nullptr) {
    writeStderr("[backtrace] [stderr] #%d: %s [%p]\n", index, symbol, address);
  } else {
    writeStderr("[backtrace] [stderr] #%d: [%p]\n", index, address);
  }
}

} // namespace Envoy
