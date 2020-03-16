#include "server/backtrace.h"

#include <iostream>

namespace Envoy {

bool BackwardsTrace::log_to_stderr_ = false;

void BackwardsTrace::setLogToStderr(bool log_to_stderr) {
  std::cerr << "setting log_to_stderr_=" << log_to_stderr << std::endl;
  log_to_stderr_ = log_to_stderr;
}

} // namespace Envoy
