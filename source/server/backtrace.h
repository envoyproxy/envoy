#pragma once

#include <backward.hpp>

#include "common/common/logger.h"

namespace Envoy {
#define BACKTRACE_LOG()                                                                            \
  do {                                                                                             \
    BackwardsTrace t;                                                                              \
    t.capture();                                                                                   \
    t.logTrace();                                                                                  \
  } while (0)

/**
 * Use the Backward library ( https://github.com/bombela/backward-cpp ) to log
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
 * To resolve the addresses in the backtrace output and de-interleave
 * multithreaded output use the tools/stack_decode.py command and pass the
 * log/stderr output to stdin of the tool. Backtrace lines will be resolved,
 * other lines will be passed through and echo'd unchanged.
 *
 * The stack_decode.py tool can also run envoy or a test as a child process if
 * you pass the command and arguments as arguments to the tool. This enables
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
  BackwardsTrace() {}

  /**
   * Capture a stack trace.
   *
   * The trace will begin with the call to capture().
   */
  void capture() { stack_trace_.load_here(MAX_STACK_DEPTH); }

  /**
   * Capture a stack trace from a particular address.
   *
   * This can be used to capture a useful stack trace from a fatal signal
   * handler.
   *
   * @param address The stack trace will begin from this address.
   */
  void captureFrom(void* address) { stack_trace_.load_from(address, MAX_STACK_DEPTH); }

  /**
   * Log the stack trace.
   */
  void logTrace() {
    backward::TraceResolver resolver;
    resolver.load_stacktrace(stack_trace_);
    // If there's nothing in the captured trace we cannot do anything.
    // The size must be at least two for useful info - there is a sentinel frame
    // at the end that we ignore.
    if (stack_trace_.size() < 2) {
      ENVOY_LOG(critical, "Back trace attempt failed");
      return;
    }

    const auto thread_id = stack_trace_.thread_id();
    backward::ResolvedTrace first_frame_trace = resolver.resolve(stack_trace_[0]);
    auto obj_name = first_frame_trace.object_filename;

#ifdef __APPLE__
    // The stack_decode.py script uses addr2line which isn't readily available and doesn't seem to
    // work when installed.
    ENVOY_LOG(critical, "Backtrace obj<{}> thr<{}>:", obj_name, thread_id);
#else
    ENVOY_LOG(critical, "Backtrace obj<{}> thr<{}> (use tools/stack_decode.py):", obj_name,
              thread_id);
#endif

    // Backtrace gets tagged by ASAN when we try the object name resolution for the last
    // frame on stack, so skip the last one. It has no useful info anyway.
    for (unsigned int i = 0; i < stack_trace_.size() - 1; ++i) {
      backward::ResolvedTrace trace = resolver.resolve(stack_trace_[i]);
      if (trace.object_filename != obj_name) {
        obj_name = trace.object_filename;
        ENVOY_LOG(critical, "thr<{}> obj<{}>", thread_id, obj_name);
      }

#ifdef __APPLE__
      // In the absence of stack_decode.py, print the function name.
      ENVOY_LOG(critical, "thr<{}> #{} {}: {}", thread_id, stack_trace_[i].idx,
                stack_trace_[i].addr, trace.object_function);
#else
      ENVOY_LOG(critical, "thr<{}> #{} {}", thread_id, stack_trace_[i].idx, stack_trace_[i].addr);
#endif
    }
    ENVOY_LOG(critical, "end backtrace thread {}", stack_trace_.thread_id());
  }

  void logFault(const char* signame, const void* addr) {
    ENVOY_LOG(critical, "Caught {}, suspect faulting address {}", signame, addr);
  }

private:
  static const int MAX_STACK_DEPTH = 64;
  backward::StackTrace stack_trace_;
};
} // Envoy
