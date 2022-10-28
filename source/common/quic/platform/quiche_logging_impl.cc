// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <atomic>
#include <cstdlib>

#include "source/common/common/utility.h"

#include "quiche_platform_impl/quiche_logging_impl.h"

namespace quiche {

namespace {

// Helper class that allows getting and setting QUICHE log verbosity threshold.
class QuicheLogVerbosityManager {
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
  static QuicheLogVerbosityManager* getSingleton() {
    static QuicheLogVerbosityManager manager = QuicheLogVerbosityManager();
    return &manager;
  }

  explicit QuicheLogVerbosityManager() {
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
std::atomic<QuicheLogSink*> q_quiche_log_sink;
absl::Mutex q_quiche_log_sink_mutex;
} // namespace

int getVerbosityLogThreshold() { return QuicheLogVerbosityManager::getThreshold(); }

void setVerbosityLogThreshold(int new_verbosity) {
  QuicheLogVerbosityManager::setThreshold(new_verbosity);
}

QuicheLogEmitter::QuicheLogEmitter(QuicheLogLevel level, const char* file_name, int line,
                                   const char* function_name)
    : level_(level), file_name_(file_name), line_(line), function_name_(function_name),
      saved_errno_(errno) {}

QuicheLogEmitter::~QuicheLogEmitter() {
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
  if (q_quiche_log_sink.load(std::memory_order_relaxed) != nullptr) {
    absl::MutexLock lock(&q_quiche_log_sink_mutex);
    QuicheLogSink* sink = q_quiche_log_sink.load(std::memory_order_relaxed);
    if (sink != nullptr) {
      sink->Log(level_, content);
    }
  }

  if (level_ == static_cast<quiche::QuicheLogLevel>(LogLevelFATAL)) {
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

QuicheLogSink* SetLogSink(QuicheLogSink* new_sink) {
  absl::MutexLock lock(&q_quiche_log_sink_mutex);
  QuicheLogSink* old_sink = q_quiche_log_sink.load(std::memory_order_relaxed);
  q_quiche_log_sink.store(new_sink, std::memory_order_relaxed);
  return old_sink;
}

} // namespace quiche
