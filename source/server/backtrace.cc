#include "server/backtrace.h"

namespace Envoy {

bool BackwardsTrace::log_to_stderr_ = false;

void BackwardsTrace::setLogToStderr(bool log_to_stderr) { log_to_stderr_ = log_to_stderr; }

} // namespace Envoy
