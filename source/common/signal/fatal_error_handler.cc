#include "common/signal/fatal_error_handler.h"

#include <list>

#include "common/common/macros.h"

#include "absl/base/attributes.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace FatalErrorHandler {

namespace {

ABSL_CONST_INIT static absl::Mutex failure_mutex(absl::kConstInit);
// Since we can't grab the failure mutex on fatal error (snagging locks under
// fatal crash causing potential deadlocks) access the handler list as an atomic
// operation, which is async-signal-safe. If the crash handler runs at the same
// time as another thread tries to modify the list, one of them will get the
// list and the other will get nullptr instead. If the crash handler loses the
// race and gets nullptr, it won't run any of the registered error handlers.
using FailureFunctionList = std::list<const FatalErrorHandlerInterface*>;
ABSL_CONST_INIT std::atomic<FailureFunctionList*> fatal_error_handlers{nullptr};

} // namespace

void registerFatalErrorHandler(const FatalErrorHandlerInterface& handler) {
#ifdef ENVOY_OBJECT_TRACE_ON_DUMP
  absl::MutexLock l(&failure_mutex);
  FailureFunctionList* list = fatal_error_handlers.exchange(nullptr, std::memory_order_relaxed);
  if (list == nullptr) {
    list = new FailureFunctionList;
  }
  list->push_back(&handler);
  fatal_error_handlers.store(list, std::memory_order_release);
#else
  UNREFERENCED_PARAMETER(handler);
#endif
}

void removeFatalErrorHandler(const FatalErrorHandlerInterface& handler) {
#ifdef ENVOY_OBJECT_TRACE_ON_DUMP
  absl::MutexLock l(&failure_mutex);
  FailureFunctionList* list = fatal_error_handlers.exchange(nullptr, std::memory_order_relaxed);
  if (list == nullptr) {
    // removeFatalErrorHandler() may see an empty list of fatal error handlers
    // if it's called at the same time as callFatalErrorHandlers(). In that case
    // Envoy is in the middle of crashing anyway, but don't add a segfault on
    // top of the crash.
    return;
  }
  list->remove(&handler);
  if (list->empty()) {
    delete list;
  } else {
    fatal_error_handlers.store(list, std::memory_order_release);
  }
#else
  UNREFERENCED_PARAMETER(handler);
#endif
}

void callFatalErrorHandlers(std::ostream& os) {
  FailureFunctionList* list = fatal_error_handlers.exchange(nullptr, std::memory_order_relaxed);
  if (list != nullptr) {
    for (const auto* handler : *list) {
      handler->onFatalError(os);
    }
    delete list;
  }
}

} // namespace FatalErrorHandler
} // namespace Envoy
