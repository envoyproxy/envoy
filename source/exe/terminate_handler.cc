#include "source/exe/terminate_handler.h"

#include <cstdlib>

#include "source/common/common/logger.h"
#include "source/server/backtrace.h"

namespace Envoy {

std::terminate_handler TerminateHandler::logOnTerminate() const {
  return std::set_terminate([]() {
    ENVOY_LOG(critical, "std::terminate called! (possible uncaught exception, see trace)");
    BACKTRACE_LOG();
    std::abort();
  });
}

} // namespace Envoy
