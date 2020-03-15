#pragma once

#include <exception>

#include "common/common/logger.h"
#include "common/common/non_copyable.h"

namespace Envoy {

class TerminateHandler : NonCopyable, protected Logger::Loggable<Logger::Id::main> {
public:
  TerminateHandler() : previous_terminate_(std::set_terminate(logStackTrace)) {}
  ~TerminateHandler() { std::set_terminate(previous_terminate_); }

  /**
   * Send stack-traces directly to stderr, rather to the logging infrastructure.
   * This is intended for coverage tests, where we enable trace logs, but send
   * them to /dev/null to avoid accumulating too much data in CI.
   *
   * @param send_to_stderr indicates whether to send stack-traces directly to stderr.
   */
  void setSendtoStderr(bool send_to_stderr);

private:
  /**
   * Sets the std::terminate to a function which will log as much of a backtrace as
   * possible, then call abort. Returns the previous handler.
   */
  std::terminate_handler logOnTerminate() const;

  static void emitStackTrace(bool send_to_stderr);
  static void logStackTrace() { emitStackTrace(false); }
  static void printStackTrace() { emitStackTrace(true); }

  const std::terminate_handler previous_terminate_;
};
} // namespace Envoy
