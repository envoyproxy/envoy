#pragma once

#include <iostream>
#include <string>

#include "source/common/common/logger.h"

#include "absl/debugging/symbolize.h"

namespace Envoy {
namespace Coroutine {

struct PromiseBase;

// Walks back current thread's coroutine stack and prints it.
class Backtrace : Logger::Loggable<Logger::Id::backtrace> {
public:
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
   * Directs all stack trace output to be formatted as a single log line
   * rather than one line per frame. This makes stack traces easier to
   * consume in log aggregation systems.
   *
   * @param single_line Whether to log the entire stack trace on a single line.
   */
  static void setSingleLine(bool single_line);

  /**
   * @return whether stack traces are formatted as a single line.
   */
  static bool singleLine() { return single_line_; }

  /**
   * Log the stack trace.
   */
  void logTrace() {
    if (log_to_stderr_) {
      printTrace(std::cerr);
      return;
    }

    if (single_line_) {
      std::string buf("Coroutine backtrace:");
      visitTrace([&buf](int index, const char* symbol, const void* address) {
        if (symbol != nullptr) {
          fmt::format_to(std::back_inserter(buf), "\n#{}: {} [{}]", index, symbol, address);
        } else {
          fmt::format_to(std::back_inserter(buf), "\n#{}: [{}]", index, address);
        }
      });
      ENVOY_LOG(critical, "{}", buf);
      return;
    }

    ENVOY_LOG(critical, "Coroutine Backtrace:");

    visitTrace([](int index, const char* symbol, const void* address) {
      if (symbol != nullptr) {
        ENVOY_LOG(critical, "#{}: {} [{}]", index, symbol, address);
      } else {
        ENVOY_LOG(critical, "#{}: [{}]", index, address);
      }
    });
  }

  void printTrace(std::ostream& os) {
    visitTrace([&](int index, const char* symbol, const void* address) {
      if (symbol != nullptr) {
        os << "#" << index << " " << symbol << " [" << address << "]\n";
      } else {
        os << "#" << index << " [" << address << "]\n";
      }
    });
  }

private:
  static bool log_to_stderr_;
  static bool single_line_;

  // Hide coroutine implementation details from this header.
  static const PromiseBase* getCurrentCoroutine();
  static const PromiseBase* getCallersPromise(const PromiseBase& promise);
  static const void* getCallerAddress(const PromiseBase& promise);

  template <typename F> void visitTrace(F visitor) {
    int index = 0;
    for (const PromiseBase* frame = getCurrentCoroutine(); frame != nullptr;
         frame = getCallersPromise(*frame)) {
      char out[1024];
      const void* address = getCallerAddress(*frame);
      const bool success = absl::Symbolize(address, out, sizeof(out));

      if (success) {
        visitor(index++, out, address);
      } else {
        visitor(index++, nullptr, address);
      }
    }
  }
};

} // namespace Coroutine
} // namespace Envoy
