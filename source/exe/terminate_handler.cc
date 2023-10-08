#include "source/exe/terminate_handler.h"

#include <cstdlib>

#include "envoy/common/exception.h"

#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/server/backtrace.h"

#include "absl/strings/str_format.h"

namespace Envoy {

std::terminate_handler TerminateHandler::logOnTerminate() const {
  return std::set_terminate([]() {
    logException(std::current_exception());
    BACKTRACE_LOG();
    std::abort();
  });
}

void TerminateHandler::logException(const std::exception_ptr current_exception) {
  if (current_exception != nullptr) {
    TRY_NEEDS_AUDIT { std::rethrow_exception(current_exception); }
    END_TRY
    MULTI_CATCH(
        const EnvoyException& e,
        {
          ENVOY_LOG(critical, "std::terminate called! Uncaught EnvoyException '{}', see trace.",
                    e.what());
        },
        { ENVOY_LOG(critical, "std::terminate called! Uncaught unknown exception, see trace."); });
  } else {
    ENVOY_LOG(critical, "std::terminate called! See trace.");
  }
}

} // namespace Envoy
