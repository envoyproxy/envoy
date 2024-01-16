#pragma once

#include <exception>

#include "source/common/common/logger.h"
#include "source/common/common/non_copyable.h"

namespace Envoy {

class TerminateHandler : NonCopyable, protected Logger::Loggable<Logger::Id::main> {
public:
  TerminateHandler() : previous_terminate_(logOnTerminate()) {}
  ~TerminateHandler() { std::set_terminate(previous_terminate_); }

private:
  friend class TerminateHandlerTest;

  /**
   * Sets the std::terminate to a function which will log as much of a backtrace as
   * possible, then call abort. Returns the previous handler.
   */
  std::terminate_handler logOnTerminate() const;

  /**
   * Logs exception details when possible
   */
  static void logException(const std::exception_ptr);

  const std::terminate_handler previous_terminate_;
};
} // namespace Envoy
