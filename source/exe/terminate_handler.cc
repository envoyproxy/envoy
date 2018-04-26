#include "exe/terminate_handler.h"

#include <cstdlib>

#include "common/common/logger.h"

#include "server/backtrace.h"

namespace Envoy {

std::terminate_handler TerminateHandler::logOnTerminate() const {
  return std::set_terminate([]() {
    ENVOY_LOG(critical, "std::terminate called! (possible uncaught exception, see trace)");
    BACKTRACE_LOG();
    std::abort();
  });
}

} // namespace Envoy
