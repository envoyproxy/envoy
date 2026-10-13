#include "source/common/signal/signal_action.h"

#include <sys/mman.h>

#include <csignal>

#include "source/common/common/assert.h"
#include "source/common/signal/fatal_action.h"
#include "source/common/version/version.h"

namespace Envoy {

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

  // Finally after logging the stack trace, call the crash handlers
  // in order from safe to unsafe.
  auto status = FatalErrorHandler::runSafeActions();

  switch (status) {
  case FatalAction::Status::Success:
    FatalErrorHandler::callFatalErrorHandlers(std::cerr);
    FatalErrorHandler::runUnsafeActions();
    break;
  case FatalAction::Status::ActionManagerUnset:
    FatalErrorHandler::callFatalErrorHandlers(std::cerr);
    break;
  case FatalAction::Status::RunningOnAnotherThread: {
    // We should wait for some duration for the other thread to finish
    // running. We should add support for this scenario, even though the
    // probability of it occurring is low.
    // TODO(kbaichoo): Implement a configurable call to sleep
    PANIC("not implemented");
    break;
  }
  case FatalAction::Status::AlreadyRanOnThisThread:
    // We caused another fatal signal to be raised.
    std::cerr << "Our FatalActions triggered a fatal signal.\n";
    break;
  }

  signal(sig, SIG_DFL);
  raise(sig);
}

void SignalAction::installSigHandlers() {
  // sigaltstack and backtrace() are incompatible on Apple platforms
  // https://reviews.llvm.org/D28265
#if !defined(__APPLE__)
  stack_t stack;
  stack.ss_sp = altstack_ + guard_size_; // Guard page at one end ...
  stack.ss_size = altstack_size_;        // ... guard page at the other
  stack.ss_flags = 0;

  RELEASE_ASSERT(sigaltstack(&stack, &previous_altstack_) == 0, "");
#endif

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
// sigaltstack and backtrace() are incompatible on Apple platforms
// https://reviews.llvm.org/D28265
#if !defined(__APPLE__)
  RELEASE_ASSERT(sigaltstack(&previous_altstack_, nullptr) == 0, "");
#endif

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
