#include "server/terminate_handler.h"

#include <cstdlib>
#include <exception>

#include "server/backtrace.h"

namespace Envoy {
namespace Server {

void logOnTerminate() {
  std::set_terminate([]() {
    BACKTRACE_LOG();
    std::abort();
  });
}

} // namespace Server
} // namespace Envoy
