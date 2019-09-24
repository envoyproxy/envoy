#include "common/signal/signal_action.h"

#include <sys/mman.h>

#include <csignal>

#include "common/common/assert.h"
#include "common/common/version.h"

namespace Envoy {

ABSL_CONST_INIT static absl::Mutex failure_mutex(absl::kConstInit);
// Since we can't grab the failure mutex on fatal error (snagging locks under
// fatal crash causing potential deadlocks) access the handler list as an atomic
// operation, to minimize the chance that one thread is operating on the list
// while the crash handler is attempting to access it.
// This basically makes edits to the list thread-safe - if one thread is
// modifying the list rather than crashing in the crash handler due to accessing
// the list in a non-thread-safe manner, it simply won't log crash traces.
using FailureFunctionList = std::list<const FatalErrorHandlerInterface*>;
ABSL_CONST_INIT std::atomic<FailureFunctionList*> fatal_error_handlers{nullptr};

void SignalAction::registerFatalErrorHandler(const FatalErrorHandlerInterface& handler) {
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

void SignalAction::removeFatalErrorHandler(const FatalErrorHandlerInterface& handler) {
#ifdef ENVOY_OBJECT_TRACE_ON_DUMP
  absl::MutexLock l(&failure_mutex);
  FailureFunctionList* list = fatal_error_handlers.exchange(nullptr, std::memory_order_relaxed);
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

constexpr int SignalAction::FATAL_SIGS[];

void SignalAction::sigHandler(int sig, siginfo_t* info, void* context) {
  BackwardsTrace tracer;

  tracer.logFault(strsignal(sig), info->si_addr);
  if (context != nullptr) {
    tracer.captureFrom(context);
  } else {
    tracer.capture();
  }
  tracer.logTrace();

  FailureFunctionList* list = fatal_error_handlers.exchange(nullptr, std::memory_order_relaxed);
  if (list) {
    // Finally after logging the stack trace, call any registered crash handlers.
    for (const auto* handler : *list) {
      handler->onFatalError();
    }
  }

  signal(sig, SIG_DFL);
  raise(sig);
}

void SignalAction::installSigHandlers() {
  stack_t stack;
  stack.ss_sp = altstack_ + guard_size_; // Guard page at one end ...
  stack.ss_size = altstack_size_;        // ... guard page at the other
  stack.ss_flags = 0;

  RELEASE_ASSERT(sigaltstack(&stack, &previous_altstack_) == 0, "");

  // Make sure VersionInfo::version() is initialized so we don't allocate std::string in signal
  // handlers.
  RELEASE_ASSERT(!VersionInfo::version().empty(), "");

  int hidx = 0;
  for (const auto& sig : FATAL_SIGS) {
    struct sigaction saction;
    std::memset(&saction, 0, sizeof(saction));
    sigemptyset(&saction.sa_mask);
    saction.sa_flags = (SA_SIGINFO | SA_ONSTACK | SA_RESETHAND | SA_NODEFER);
    saction.sa_sigaction = sigHandler;
    auto* handler = &previous_handlers_[hidx++];
    RELEASE_ASSERT(sigaction(sig, &saction, handler) == 0, "");
  }
}

void SignalAction::removeSigHandlers() {
#if defined(__APPLE__)
  // ss_flags contains SS_DISABLE, but Darwin still checks the size, contrary to the man page
  if (previous_altstack_.ss_size < MINSIGSTKSZ) {
    previous_altstack_.ss_size = MINSIGSTKSZ;
  }
#endif
  RELEASE_ASSERT(sigaltstack(&previous_altstack_, nullptr) == 0, "");

  int hidx = 0;
  for (const auto& sig : FATAL_SIGS) {
    auto* handler = &previous_handlers_[hidx++];
    RELEASE_ASSERT(sigaction(sig, handler, nullptr) == 0, "");
  }
}

#if defined(__APPLE__) && !defined(MAP_STACK)
#define MAP_STACK (0)
#endif

void SignalAction::mapAndProtectStackMemory() {
  // Per docs MAP_STACK doesn't actually do anything today but provides a
  // library hint that might be used in the future.
  altstack_ = static_cast<char*>(mmap(nullptr, mapSizeWithGuards(), PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0));
  RELEASE_ASSERT(altstack_, "");
  RELEASE_ASSERT(mprotect(altstack_, guard_size_, PROT_NONE) == 0, "");
  RELEASE_ASSERT(mprotect(altstack_ + guard_size_ + altstack_size_, guard_size_, PROT_NONE) == 0,
                 "");
}

void SignalAction::unmapStackMemory() { munmap(altstack_, mapSizeWithGuards()); }

void SignalAction::doGoodAccessForTest() {
  volatile char* altaltstack = altstack_;
  for (size_t i = 0; i < altstack_size_; ++i) {
    *(altaltstack + guard_size_ + i) = 42;
  }
  for (size_t i = 0; i < altstack_size_; ++i) {
    ASSERT(*(altaltstack + guard_size_ + i) == 42);
  }
}

void SignalAction::tryEvilAccessForTest(bool end) {
  volatile char* altaltstack = altstack_;
  if (end) {
    // One byte past the valid region
    // http://oeis.org/A001969
    *(altaltstack + guard_size_ + altstack_size_) = 43;
  } else {
    // One byte before the valid region
    *(altaltstack + guard_size_ - 1) = 43;
  }
}
} // namespace Envoy
