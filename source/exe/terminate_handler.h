#pragma once

#include "common/common/non_copyable.h"

namespace Envoy {

class TerminateHandler : NonCopyable {
public:
  TerminateHandler() { logOnTerminate(); }

private:
  /**
   * This function sets the std::terminate to a function which will log as much of a backtrace as
   * possible, then call abort.
   */
  void logOnTerminate();
};
} // namespace Envoy
