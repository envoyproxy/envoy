#include "source/common/signal/non_fatal_signal_handler.h"

#include <atomic>
#include <csignal>
#include <cstring>

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"

namespace Envoy {
namespace NonFatalSignalHandler {

namespace {

std::atomic<NonFatalSignalCallback> handlers[MaxHandlers]{};
std::atomic<bool> installed = false;

struct sigaction previous_handler;

int registered_count = 0;

void sigHandler(int sig, siginfo_t* info, void* context) {
  callNonFatalSignalHandlers(sig, info, context);
}

void installSigHandler() {
  struct sigaction saction;
  std::memset(&saction, 0, sizeof(saction));
  sigemptyset(&saction.sa_mask);
  saction.sa_flags = SA_SIGINFO;
  saction.sa_sigaction = sigHandler;
  RELEASE_ASSERT(sigaction(SIGUSR2, &saction, &previous_handler) == 0, "");
  installed.store(true, std::memory_order_release);
}

void removeSigHandler() {
  RELEASE_ASSERT(sigaction(SIGUSR2, &previous_handler, nullptr) == 0, "");
  installed.store(false, std::memory_order_release);
}

} // namespace

bool isInstalled() { return installed.load(std::memory_order_acquire); }

bool registerNonFatalSignalHandler(NonFatalSignalCallback cb) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  for (auto& slot : handlers) {
    if (slot.load(std::memory_order_relaxed) == nullptr) {
      slot.store(cb, std::memory_order_release);
      if (registered_count++ == 0) {
        installSigHandler();
      }
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
      if (--registered_count == 0) {
        removeSigHandler();
      }
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
