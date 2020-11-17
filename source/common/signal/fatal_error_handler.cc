#include "common/signal/fatal_error_handler.h"

#include <atomic>
#include <list>

#include "envoy/event/dispatcher.h"

#include "common/common/assert.h"
#include "common/common/macros.h"
#include "common/signal/fatal_action.h"

#include "absl/base/attributes.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace FatalErrorHandler {

namespace {

// The type of Fatal Actions.
enum class FatalActionType {
  Safe,
  Unsafe,
};

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
ABSL_CONST_INIT std::atomic<int64_t> failure_tid{-1};

// Executes the Fatal Actions provided.
void runFatalActionsInternal(const FatalAction::FatalActionPtrList& actions) {
  // Exchange the fatal_error_handlers pointer so other functions cannot
  // concurrently access the list. Use memory order acquire_release since
  // exchange is a read-modify-write operation.
  FailureFunctionList* list = fatal_error_handlers.exchange(nullptr, std::memory_order_acq_rel);
  if (list == nullptr) {
    return;
  }

  // Get the dispatcher and its tracked object.
  for (auto* handler : *list) {
    handler->runFatalActionsOnTrackedObject(actions);
  }

  // Restore the fatal_error_handlers pointer so subsequent calls using the list
  // can succeed.
  fatal_error_handlers.store(list, std::memory_order_release);
}

// Helper function to run exclusively either safe or unsafe actions depending on
// the provided action_type.
// Returns a FatalAction status corresponding to our attempt to run the
// action_type.
FatalAction::Status runFatalActions(FatalActionType action_type) {
  // Check that registerFatalActions has already been called.
  FatalAction::FatalActionManager* action_manager =
      fatal_action_manager.load(std::memory_order_acquire);

  if (action_manager == nullptr) {
    return FatalAction::Status::ActionManangerUnset;
  }

  int64_t my_tid = action_manager->getThreadFactory().currentThreadId().getId();

  if (action_type == FatalActionType::Safe) {
    // Try to run safe actions
    int64_t expected_tid = -1;

    if (failure_tid.compare_exchange_strong(expected_tid, my_tid, std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
      // Run the actions
      runFatalActionsInternal(action_manager->getSafeActions());
      return FatalAction::Status::Success;
    } else if (expected_tid == my_tid) {
      return FatalAction::Status::AlreadyRanOnThisThread;
    }

  } else {
    // Try to run unsafe actions
    int64_t failing_tid = failure_tid.load(std::memory_order_acquire);

    if (my_tid == failing_tid) {
      runFatalActionsInternal(action_manager->getUnsafeActions());
      return FatalAction::Status::Success;
    } else if (failing_tid == -1) {
      // This shouldn't occur unless the module is being used incorrectly.
      return FatalAction::Status::SafeActionsNotYetRan;
    }
  }

  return FatalAction::Status::RunningOnAnotherThread;
}

} // namespace

void registerFatalErrorHandler(const FatalErrorHandlerInterface& handler) {
#ifdef ENVOY_OBJECT_TRACE_ON_DUMP
  absl::MutexLock l(&failure_mutex);
  // Using memory_order_relaxed should be okay since calls to this function only
  // occur when we allocate dispatchers, which only happens in the main thread
  // at startup.
  FailureFunctionList* list = fatal_error_handlers.exchange(nullptr, std::memory_order_relaxed);
  if (list == nullptr) {
    list = new FailureFunctionList;
  }
  list->push_back(&handler);
  // Store the fatal_error_handlers pointer now that the list is updated. Using
  // memory_order_release should work since all accesses to the list apart from the one
  // above use at least std::memory_order_acquire.
  fatal_error_handlers.store(list, std::memory_order_release);
#else
  UNREFERENCED_PARAMETER(handler);
#endif
}

void removeFatalErrorHandler(const FatalErrorHandlerInterface& handler) {
#ifdef ENVOY_OBJECT_TRACE_ON_DUMP
  absl::MutexLock l(&failure_mutex);
  // Use memory order acquire_release since exchange is a read-modify-write operation.
  FailureFunctionList* list = fatal_error_handlers.exchange(nullptr, std::memory_order_acq_rel);
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
    // Use memory_order_release, other accesses use at least acquire semantics.
    fatal_error_handlers.store(list, std::memory_order_release);
  }
#else
  UNREFERENCED_PARAMETER(handler);
#endif
}

void callFatalErrorHandlers(std::ostream& os) {
  // Use memory order acquire_release since exchange is a read-modify-write operation.
  FailureFunctionList* list = fatal_error_handlers.exchange(nullptr, std::memory_order_acq_rel);
  if (list != nullptr) {
    for (const auto* handler : *list) {
      handler->onFatalError(os);
    }

    // Use memory_order_release, other accesses use at least acquire semantics.
    fatal_error_handlers.store(list, std::memory_order_release);
  }
}

void registerFatalActions(FatalAction::FatalActionPtrList safe_actions,
                          FatalAction::FatalActionPtrList unsafe_actions,
                          Thread::ThreadFactory& thread_factory) {
  // Create a FatalActionManager and try to store it. If we fail to store
  // our manager, it'll be deleted due to the unique_ptr.
  auto manager = std::make_unique<FatalAction::FatalActionManager>(
      std::move(safe_actions), std::move(unsafe_actions), thread_factory);
  FatalAction::FatalActionManager* unset_manager = nullptr;

  if (fatal_action_manager.compare_exchange_strong(unset_manager, manager.get(),
                                                   std::memory_order_acq_rel)) {
    // Our manager is the system's singleton, ensure that the unique_ptr does not
    // delete the instance.
    manager.release();
  } else {
    ENVOY_BUG(false, "Fatal Actions have already been registered.");
  }
}

FatalAction::Status runSafeActions() { return runFatalActions(FatalActionType::Safe); }

FatalAction::Status runUnsafeActions() { return runFatalActions(FatalActionType::Unsafe); }

void clearFatalActionsOnTerminate() {
  auto* raw_ptr = fatal_action_manager.exchange(nullptr, std::memory_order_seq_cst);
  if (raw_ptr != nullptr) {
    delete raw_ptr;
  }
}

// This resets the internal state of Fatal Action for the module.
// This is necessary as it allows us to have multiple test cases invoke the
// fatal actions without state from other tests leaking in.
void resetFatalActionStateForTest() {
  // Free the memory of the Fatal Action, since it's not managed by a smart
  // pointer. This prevents memory leaks in tests.
  auto* raw_ptr = fatal_action_manager.exchange(nullptr, std::memory_order_seq_cst);
  if (raw_ptr != nullptr) {
    delete raw_ptr;
  }
  failure_tid.store(-1, std::memory_order_release);
}

} // namespace FatalErrorHandler
} // namespace Envoy
