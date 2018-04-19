#include "exe/terminate_handler.h"

#include <cstdlib>

#include "server/backtrace.h"

namespace Envoy {

std::terminate_handler TerminateHandler::logOnTerminate() const {
  return std::set_terminate([]() {
    BACKTRACE_LOG();
    std::abort();
  });
}

} // namespace Envoy
