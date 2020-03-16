#pragma once

#include <exception>

#include "common/common/logger.h"
#include "common/common/non_copyable.h"

namespace Envoy {

class TerminateHandler : NonCopyable, protected Logger::Loggable<Logger::Id::main> {
public:
  TerminateHandler() : previous_terminate_(logOnTerminate()) {}
  ~TerminateHandler() { std::set_terminate(previous_terminate_); }

private:
  /**
   * Sets the std::terminate to a function which will log as much of a backtrace as
   * possible, then call abort. Returns the previous handler.
   */
  std::terminate_handler logOnTerminate() const;

  const std::terminate_handler previous_terminate_;
};
} // namespace Envoy
