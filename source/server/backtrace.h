#pragma once

#include "common/common/logger.h"

#include "backward.hpp"

#define BACKTRACE_LOG()                                                                            \
  do {                                                                                             \
    BackwardsTrace t;                                                                              \
    t.Capture();                                                                                   \
    t.Log();                                                                                       \
  } while (0)

#define BACKTRACE_PROD_LOG()                                                                       \
  do {                                                                                             \
    BackwardsTrace t(true);                                                                        \
    t.Capture();                                                                                   \
    t.Log();                                                                                       \
  } while (0)

/**
 * Use the Backward library ( https://github.com/bombela/backward-cpp ) to log
 * stack traces on demand.  To use this just do:
 *
 * BackwardsTrace tracer;
 * tracer.Capture(); // Trace is captured as of here.
 * tracer.Log(); // Output the captured trace to the log.
 *
 * The capture and log steps are separated to enable debugging in the case where
 * you want to capture a stack trace from inside some logic but don't know whether
 * you want to bother logging it until later.
 *
 * For convenience a macro is provided BACKTRACE_LOG() which performs the
 * construction, capture, and log in one shot. Use BACKTRACE_PROD_LOG() if you
 * want the logs to appear in production (critical level, with NDEBUG)
 *
 * To resolve the addresses in the backtrace output and de-interleave
 * multithreaded output use the tools/stack_decode.py command and pass the
 * log/stderr output to stdin of the tool.  Backtrace lines will be resolved,
 * other lines will be passed through and echo'd unchanged.
 *
 * The stack_decode.py tool can also run envoy or a test as a child process if
 * you pass the command and arguments as arguments to the tool.  This enables
 * you to run tests containing backtrace commands added for debugging and see
 * the output like this:
 *
 *     bazel test -c dbg //test/server:backtrace_test
 *      --run_under=`pwd`/tools/stack_decode.py
 *      --strategy=TestRunner=standalone --cache_test_results=no
 *      --test_output=all
 */
class BackwardsTrace : Logger::Loggable<Logger::Id::backtrace> {
public:
  /**
   * Construct a BackwardsTrace with optional production enablement.
   *
   * If the optional argument log_in_prod is true then this BackwardsTrace will
   * capture even when NDEBUG is defined and will log at critical level. This
   * allows defining backtrace captures associated with PANIC() and other
   * serious errors encountered in real production while also defining backtrace
   * captures used only in debug builds.
   *
   * By default log_in_prod is false and if NDEBUG is defined these methods are
   * all no-ops. Backtraces will be logged at debug level when NDEBUG is not defined.
   *
   * @param log_in_prod Log at a critical level so we see output even in
   * optimized builds.
   */
  BackwardsTrace(const bool log_in_prod = false) : log_in_prod_(log_in_prod) {}

  /**
   * Capture a stack trace.
   *
   * The trace will begin with the call to Capture().
   */
  void Capture() {
#ifdef NDEBUG
    if (!log_in_prod_) {
      return;
    }
#endif
    stack_trace_.load_here(MAX_STACK_DEPTH);
  }

  /**
   * Capture a stack trace from a particular address.
   *
   * This can be used to capture a useful stack trace from a fatal signal
   * handler.
   *
   * @param address The stack trace will begin from this address.
   */
  void CaptureFrom(void* address) {
#ifdef NDEBUG
    if (!log_in_prod_) {
      return;
    }
#endif
    stack_trace_.load_from(address, MAX_STACK_DEPTH);
  }

  /**
   * Log the stack trace.
   */
  void Log() {
#ifdef NDEBUG
    if (!log_in_prod_) {
      return;
    }
#endif
    backward::TraceResolver resolver;
    resolver.load_stacktrace(stack_trace_);
    // If there's nothing in the captured trace we cannot do anything.
    if (stack_trace_.size() < 1) {
      LogAtLevel("Back trace attempt failed");
      return;
    }

    const auto thread_id = stack_trace_.thread_id();
    backward::ResolvedTrace first_frame_trace = resolver.resolve(stack_trace_[0]);
    auto obj_name = first_frame_trace.object_filename;

    LogAtLevel("Backtrace obj<{}> thr<{}> (use tools/stack_decode.py):", obj_name, thread_id);

    // Why start at 2? To hide the function call to backward that began the
    // trace.
    for (unsigned int i = 0; i < stack_trace_.size(); ++i) {
      // Backtrace gets tagged by ASAN when we try the object name resolution for the last
      // frame on stack, so skip the last one. It has no useful info anyway.
      if (i + 1 != stack_trace_.size()) {
        backward::ResolvedTrace trace = resolver.resolve(stack_trace_[i]);
        if (trace.object_filename != obj_name) {
          obj_name = trace.object_filename;
          LogAtLevel("thr<{}> obj<{}>", thread_id, obj_name);
        }
      }
      LogAtLevel("thr<{}> #{} {}", thread_id, stack_trace_[i].idx, stack_trace_[i].addr);
    }
    LogAtLevel("end backtrace thread {}", stack_trace_.thread_id());
  }

  void LogFault(const char* signame, const void* addr) {
    LogAtLevel("Caught {}, suspect faulting address {}", signame, addr);
  }

private:
  template <typename T, typename... Args> void LogAtLevel(T format_str, Args... args) {
    if (log_in_prod_) {
      log().critical(format_str, args...);
    } else {
      log().debug(format_str, args...);
    }
  }
  static const int MAX_STACK_DEPTH = 64;
  backward::StackTrace stack_trace_;
  const bool log_in_prod_;
};
