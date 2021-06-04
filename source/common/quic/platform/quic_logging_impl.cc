// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "source/common/quic/platform/quic_logging_impl.h"

#include <atomic>
#include <cstdlib>

#include "source/common/common/utility.h"

namespace quic {

namespace {

// Helper class that allows getting and setting QUICHE log verbosity threshold.
class QuicLogVerbosityManager {
public:
  // Gets QUICHE log verbosity threshold.
  static int getThreshold() {
    return getSingleton()->verbosity_threshold_.load(std::memory_order_relaxed);
  }

  // Sets QUICHE log verbosity threshold.
  static void setThreshold(int new_verbosity) {
    getSingleton()->verbosity_threshold_.store(new_verbosity, std::memory_order_relaxed);
  }

private:
  static QuicLogVerbosityManager* getSingleton() {
    static QuicLogVerbosityManager manager = QuicLogVerbosityManager();
    return &manager;
  }

  explicit QuicLogVerbosityManager() {
    int verbosity = 0;
    const char* verbosity_str = std::getenv("ENVOY_QUICHE_VERBOSITY");
    if (verbosity_str != nullptr) {
      verbosity = atoi(verbosity_str);
    }
    verbosity_threshold_.store(verbosity, std::memory_order_relaxed);
  }

  std::atomic<int> verbosity_threshold_;
};

std::atomic<bool> g_dfatal_exit_disabled;

// Pointer to the global log sink, usually it is nullptr.
// If not nullptr, as in some tests, the sink will receive a copy of the log message right after the
// message is emitted from the QUIC_LOG... macros.
std::atomic<QuicLogSink*> g_quic_log_sink;
absl::Mutex g_quic_log_sink_mutex;
} // namespace

int getVerbosityLogThreshold() { return QuicLogVerbosityManager::getThreshold(); }

void setVerbosityLogThreshold(int new_verbosity) {
  QuicLogVerbosityManager::setThreshold(new_verbosity);
}

QuicLogEmitter::QuicLogEmitter(QuicLogLevel level, const char* file_name, int line,
                               const char* function_name)
    : level_(level), file_name_(file_name), line_(line), function_name_(function_name),
      saved_errno_(errno) {}

QuicLogEmitter::~QuicLogEmitter() {
  if (is_perror_) {
    // TODO(wub): Change to a thread-safe version of errorDetails.
    stream_ << ": " << Envoy::errorDetails(saved_errno_) << " [" << saved_errno_ << "]";
  }
  std::string content = stream_.str();
  if (!content.empty() && content.back() == '\n') {
    // strip the last trailing '\n' because spd log will add a trailing '\n' to
    // the output.
    content.back() = '\0';
  }
  GetLogger().log(::spdlog::source_loc(file_name_, line_, function_name_), level_, "{}",
                  content.c_str());

  // Normally there is no log sink and we can avoid acquiring the lock.
  if (g_quic_log_sink.load(std::memory_order_relaxed) != nullptr) {
    absl::MutexLock lock(&g_quic_log_sink_mutex);
    QuicLogSink* sink = g_quic_log_sink.load(std::memory_order_relaxed);
    if (sink != nullptr) {
      sink->Log(level_, content);
    }
  }

  if (level_ == static_cast<quic::QuicLogLevel>(LogLevelFATAL)) {
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

bool isDFatalExitDisabled() { return g_dfatal_exit_disabled.load(std::memory_order_relaxed); }

void setDFatalExitDisabled(bool is_disabled) {
  g_dfatal_exit_disabled.store(is_disabled, std::memory_order_relaxed);
}

QuicLogSink* SetLogSink(QuicLogSink* new_sink) {
  absl::MutexLock lock(&g_quic_log_sink_mutex);
  QuicLogSink* old_sink = g_quic_log_sink.load(std::memory_order_relaxed);
  g_quic_log_sink.store(new_sink, std::memory_order_relaxed);
  return old_sink;
}

} // namespace quic
