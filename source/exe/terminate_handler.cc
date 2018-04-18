#include "exe/terminate_handler.h"

#include <cstdlib>
#include <exception>

#include "server/backtrace.h"

namespace Envoy {

void TerminateHandler::logOnTerminate() {
  std::set_terminate([]() {
    BACKTRACE_LOG();
    std::abort();
  });
}

} // namespace Envoy
