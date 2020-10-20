#include "common/signal/fatal_error_handler.h"

#include <list>

#include "envoy/event/dispatcher.h"

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

// Use an atomic operation since on fatal error we'll consume the
// fatal_action_manager and don't want to have any locks as they aren't
// async-signal-safe.
ABSL_CONST_INIT std::atomic<FatalAction::FatalActionManager*> fatal_action_manager{nullptr};
ABSL_CONST_INIT std::atomic<Envoy::Server::Instance*> server_instance{nullptr};
ABSL_CONST_INIT std::atomic<int64_t> failure_tid{-1};

using FatalActionPtrList = std::list<const Server::Configuration::FatalActionPtr>;

// Helper function to run fatal actions.
void runFatalActions(FatalActionPtrList& actions) {
  FailureFunctionList* list = fatal_error_handlers.exchange(nullptr, std::memory_order_relaxed);
  if (list == nullptr) {
    return;
  }

  // Get the dispatcher and its tracked object.
  for (auto* handler : *list) {
    handler->runFatalActionsOnTrackedObject(actions);
  }

  fatal_error_handlers.store(list, std::memory_order_release);
}
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
    fatal_error_handlers.store(list, std::memory_order_release);
  }
}

void registerFatalActions(std::unique_ptr<FatalActionPtrList> safe_actions,
                          std::unique_ptr<FatalActionPtrList> unsafe_actions,
                          Envoy::Server::Instance* server) {
  static bool registered_actions = false;
  if (registered_actions) {
    ENVOY_BUG(false, "registerFatalActions called more than once.");
    return;
  }

  // Create a FatalActionManager and try to store it. If we fail to store
  // our manager, it'll be deleted due to the unique_ptr.
  auto mananger = std::make_unique<FatalAction::FatalActionManager>(std::move(safe_actions),
                                                                    std::move(unsafe_actions));
  FatalAction::FatalActionManager* unset_manager = nullptr;

  if (fatal_action_manager.compare_exchange_strong(unset_manager, mananger.get(),
                                                   std::memory_order_acq_rel)) {
    registered_actions = true;
    server_instance.store(server, std::memory_order_release);
    // Our manager is the system singleton, ensure that the unique_ptr does not
    // delete the instance.
    mananger.release();
  }
}

bool runSafeActions() {
  // Check that registerFatalActions has already been called.
  FatalAction::FatalActionManager* action_manager =
      fatal_action_manager.load(std::memory_order_acquire);
  Envoy::Server::Instance* server = server_instance.load(std::memory_order_acquire);

  if (action_manager == nullptr || server == nullptr) {
    return false;
  }

  // Check that we're the thread that gets to run the actions.
  int64_t my_tid = server->api().threadFactory().currentThreadId().getId();
  int64_t expected_tid = -1;

  if (failure_tid.compare_exchange_strong(expected_tid, my_tid, std::memory_order_release,
                                          std::memory_order_relaxed)) {
    // Run the actions
    runFatalActions(action_manager->getSafeActions());
    return true;
  }

  return false;
}

bool runUnsafeActions() {
  // Check that registerFatalActions has already been called.
  FatalAction::FatalActionManager* action_manager =
      fatal_action_manager.load(std::memory_order_acquire);
  Envoy::Server::Instance* server = server_instance.load(std::memory_order_acquire);

  if (action_manager == nullptr || server == nullptr) {
    return false;
  }

  // Check that we're the thread that gets to run the actions.
  int64_t my_tid = server->api().threadFactory().currentThreadId().getId();

  if (my_tid == failure_tid.load(std::memory_order_acquire)) {
    // Run the actions
    runFatalActions(action_manager->getUnsafeActions());
    return true;
  }
  return false;
}

} // namespace FatalErrorHandler
} // namespace Envoy
