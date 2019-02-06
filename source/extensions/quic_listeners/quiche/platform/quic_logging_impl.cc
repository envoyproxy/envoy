// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_logging_impl.h"

#include <atomic>

namespace quic {

namespace {
std::atomic<int>& VerbosityLogThreshold() {
  static std::atomic<int> threshold(0);
  return threshold;
}
} // namespace

QuicLogEmitter::QuicLogEmitter(QuicLogLevel level) : level_(level), saved_errno_(errno) {}

QuicLogEmitter::~QuicLogEmitter() {
  if (is_perror_) {
    // TODO(wub): Change to a thread-safe version of strerror.
    stream_ << ": " << strerror(saved_errno_) << " [" << saved_errno_ << "]";
  }
  GetLogger().log(level_, "quic: {}", stream_.str().c_str());
  if (level_ == FATAL) {
    abort();
  }
}

int GetVerbosityLogThreshold() { return VerbosityLogThreshold().load(std::memory_order_relaxed); }

void SetVerbosityLogThreshold(int new_verbosity) {
  VerbosityLogThreshold().store(new_verbosity, std::memory_order_relaxed);
}

} // namespace quic