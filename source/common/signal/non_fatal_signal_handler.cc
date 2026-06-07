#include "source/common/signal/non_fatal_signal_handler.h"

#include <atomic>

#include "source/common/common/logger.h"
#include "source/common/common/thread.h"

namespace Envoy {
namespace NonFatalSignalHandler {

namespace {

static std::atomic<NonFatalSignalCallback> handlers[MaxHandlers]{};

} // namespace

bool registerNonFatalSignalHandler(NonFatalSignalCallback cb) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  for (auto& slot : handlers) {
    if (slot.load(std::memory_order_relaxed) == nullptr) {
      slot.store(cb, std::memory_order_release);
      return true;
    }
  }
  ENVOY_LOG_MISC(error, "Failed to register non-fatal signal handler: max handlers ({}) reached",
                 MaxHandlers);
  return false;
}

void removeNonFatalSignalHandler(NonFatalSignalCallback cb) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  for (auto& slot : handlers) {
    if (slot.load(std::memory_order_relaxed) == cb) {
      slot.store(nullptr, std::memory_order_release);
      return;
    }
  }
}

void callNonFatalSignalHandlers(int sig, siginfo_t* info, void* context) {
  for (auto& slot : handlers) {
    NonFatalSignalCallback cb = slot.load(std::memory_order_acquire);
    if (cb != nullptr) {
      cb(sig, info, context);
    }
  }
}

} // namespace NonFatalSignalHandler
} // namespace Envoy
