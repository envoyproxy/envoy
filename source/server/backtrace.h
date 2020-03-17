#pragma once

#include <functional>

#include "common/common/logger.h"
#include "common/common/version.h"

#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"

namespace Envoy {
#define BACKTRACE_LOG()                                                                            \
  do {                                                                                             \
    BackwardsTrace t;                                                                              \
    t.capture();                                                                                   \
    t.logTrace();                                                                                  \
  } while (0)

/**
 * Use absl::Stacktrace and absl::Symbolize to log resolved symbols
 * stack traces on demand. To use this just do:
 *
 * BackwardsTrace tracer;
 * tracer.capture(); // Trace is captured as of here.
 * tracer.logTrace(); // Output the captured trace to the log.
 *
 * The capture and log steps are separated to enable debugging in the case where
 * you want to capture a stack trace from inside some logic but don't know whether
 * you want to bother logging it until later.
 *
 * For convenience a macro is provided BACKTRACE_LOG() which performs the
 * construction, capture, and log in one shot.
 *
 * If the symbols cannot be resolved by absl::Symbolize then the raw address
 * will be printed instead.
 */
class BackwardsTrace : Logger::Loggable<Logger::Id::backtrace> {
public:
  BackwardsTrace() = default;

  /**
   * Directs the output of logTrace() to directly stderr rather than the
   * logging infrastructure.
   *
   * This is intended for coverage tests, where we enable trace logs, but send
   * them to /dev/null to avoid accumulating too much data in CI.
   *
   * @param log_to_stderr Whether to log to stderr or the logging system.
   */
  static void setLogToStderr(bool log_to_stderr);

  /**
   * @return whether the system directing backtraces directly to stderr.
   */
  static bool logToStderr() { return log_to_stderr_; }

  /**
   * Capture a stack trace.
   *
   * The trace will begin with the call to capture().
   */
  void capture() {
    // Skip of one means we exclude the last call, which must be to capture().
    stack_depth_ = absl::GetStackTrace(stack_trace_, MaxStackDepth, /* skip_count = */ 1);
  }

  /**
   * Capture a stack trace from a particular context.
   *
   * This can be used to capture a useful stack trace from a fatal signal
   * handler. The context argument should be a pointer to the context passed
   * to a signal handler registered via a sigaction struct.
   *
   * @param context A pointer to ucontext_t obtained from a sigaction handler.
   */
  void captureFrom(const void* context) {
    stack_depth_ =
        absl::GetStackTraceWithContext(stack_trace_, MaxStackDepth, /* skip_count = */ 1, context,
                                       /* min_dropped_frames = */ nullptr);
  }

  /**
   * Log the stack trace.
   */
  void logTrace() {
    if (log_to_stderr_) {
      printTrace(std::cerr);
      return;
    }

    ENVOY_LOG(critical, "Backtrace (use tools/stack_decode.py to get line numbers):");
    ENVOY_LOG(critical, "Envoy version: {}", VersionInfo::version());

    visitTrace([](int index, const char* symbol, void* address) {
      if (symbol != nullptr) {
        ENVOY_LOG(critical, "#{}: {} [{}]", index, symbol, address);
      } else {
        ENVOY_LOG(critical, "#{}: [{}]", index, address);
      }
    });
  }

  void logFault(const char* signame, const void* addr) {
    ENVOY_LOG(critical, "Caught {}, suspect faulting address {}", signame, addr);
  }

  void printTrace(std::ostream& os) {
    visitTrace([&](int index, const char* symbol, void* address) {
      if (symbol != nullptr) {
        os << "#" << index << " " << symbol << " [" << address << "]\n";
      } else {
        os << "#" << index << " [" << address << "]\n";
      }
    });
  }

private:
  static bool log_to_stderr_;

  /**
   * Visit the previously captured stack trace.
   *
   * The visitor function is called once per frame, with 3 parameters:
   * 1. (int) The index of the current frame.
   * 2. (const char*) The symbol name for the address of the current frame. nullptr means
   * symbolization failed.
   * 3. (void*) The address of the current frame.
   */
  void visitTrace(const std::function<void(int, const char*, void*)>& visitor) {
    for (int i = 0; i < stack_depth_; ++i) {
      char out[1024];
      const bool success = absl::Symbolize(stack_trace_[i], out, sizeof(out));
      if (success) {
        visitor(i, out, stack_trace_[i]);
      } else {
        visitor(i, nullptr, stack_trace_[i]);
      }
    }
  }

  static constexpr int MaxStackDepth = 64;
  void* stack_trace_[MaxStackDepth];
  int stack_depth_{0};
};
} // namespace Envoy
