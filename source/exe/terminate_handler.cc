#include "exe/terminate_handler.h"

#include <cstdlib>

#include "common/common/logger.h"

#include "server/backtrace.h"

namespace Envoy {

std::terminate_handler TerminateHandler::logOnTerminate() const {
  return std::set_terminate([]() { emitStackTrace(false); });
}

void TerminateHandler::setSendtoStderr(bool send_to_stderr) {
  if (send_to_stderr) {
    std::set_terminate(printStackTrace);
  } else {
    std::set_terminate(logStackTrace);
  }
}

void TerminateHandler::emitStackTrace(bool send_to_stderr) {
  static const char msg[] = "std::terminate called! (possible uncaught exception, see trace)";
  if (send_to_stderr) {
    std::cerr << msg << std::endl;
  } else {
    ENVOY_LOG(critical, "{}", msg);
  }
  BackwardsTrace t;
  t.capture();
  if (send_to_stderr) {
    t.printTrace(std::cerr);
  } else {
    t.logTrace();
  }
  std::abort();
}

} // namespace Envoy
