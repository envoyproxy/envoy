#include "source/exe/terminate_handler.h"

#include <cstdlib>

#include "envoy/common/exception.h"

#include "source/common/common/logger.h"
#include "source/server/backtrace.h"

#include "absl/strings/str_format.h"

namespace Envoy {

std::terminate_handler TerminateHandler::logOnTerminate() const {
  return std::set_terminate([]() {
    std::exception_ptr current_exception = std::current_exception();
    if (current_exception != nullptr) {
      try {
        std::rethrow_exception(current_exception);
      } catch (const EnvoyException& e) {
        ENVOY_LOG(critical,
                  "std::terminate called! Uncaught EnvoyException '{}', see trace.", e.what());
      } catch (const std::exception& e) {
        ENVOY_LOG(critical,
                  "std::terminate called! Uncaught exception '{}', see trace.", e.what());
      } catch (...) {
        ENVOY_LOG(critical,
                 "std::terminate called! See trace. Uncaught unknown exception, see trace.");
      }
    } else {
      ENVOY_LOG(critical, "std::terminate called! See trace.");
    }
    BACKTRACE_LOG();
    std::abort();
  });
}

} // namespace Envoy
