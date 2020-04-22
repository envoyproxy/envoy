// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_logging_impl.h"

#include <atomic>

namespace quic {

namespace {
std::atomic<int> g_verbosity_threshold;
std::atomic<bool> g_dfatal_exit_disabled;

// Pointer to the global log sink, usually it is nullptr.
// If not nullptr, as in some tests, the sink will receive a copy of the log message right after the
// message is emitted from the QUIC_LOG... macros.
std::atomic<QuicLogSink*> g_quic_log_sink;
absl::Mutex g_quic_log_sink_mutex;
} // namespace

QuicLogEmitter::QuicLogEmitter(QuicLogLevel level) : level_(level), saved_errno_(errno) {}

QuicLogEmitter::~QuicLogEmitter() {
  if (is_perror_) {
    // TODO(wub): Change to a thread-safe version of strerror.
    stream_ << ": " << strerror(saved_errno_) << " [" << saved_errno_ << "]";
  }
  std::string content = stream_.str();
  if (!content.empty() && content.back() == '\n') {
    // strip the last trailing '\n' because spd log will add a trailing '\n' to
    // the output.
    content.back() = '\0';
  }
  GetLogger().log(level_, "{}", content.c_str());

  // Normally there is no log sink and we can avoid acquiring the lock.
  if (g_quic_log_sink.load(std::memory_order_relaxed) != nullptr) {
    absl::MutexLock lock(&g_quic_log_sink_mutex);
    QuicLogSink* sink = g_quic_log_sink.load(std::memory_order_relaxed);
    if (sink != nullptr) {
      sink->Log(level_, content);
    }
  }

  if (level_ == FATAL) {
    GetLogger().flush();
#ifdef NDEBUG
    // Release mode.
    abort();
#else
    // Debug mode.
    if (!g_dfatal_exit_disabled) {
      abort();
    }
#endif
  }
}

int GetVerbosityLogThreshold() { return g_verbosity_threshold.load(std::memory_order_relaxed); }

void SetVerbosityLogThreshold(int new_verbosity) {
  g_verbosity_threshold.store(new_verbosity, std::memory_order_relaxed);
}

bool IsDFatalExitDisabled() { return g_dfatal_exit_disabled.load(std::memory_order_relaxed); }

void SetDFatalExitDisabled(bool is_disabled) {
  g_dfatal_exit_disabled.store(is_disabled, std::memory_order_relaxed);
}

QuicLogSink* SetLogSink(QuicLogSink* new_sink) {
  absl::MutexLock lock(&g_quic_log_sink_mutex);
  QuicLogSink* old_sink = g_quic_log_sink.load(std::memory_order_relaxed);
  g_quic_log_sink.store(new_sink, std::memory_order_relaxed);
  return old_sink;
}

} // namespace quic
