#include "exe/signal_action.h"

#include <signal.h>
#include <sys/mman.h>

#include "common/common/assert.h"

constexpr int SignalAction::FATAL_SIGS[];

void SignalAction::sigHandler(int sig, siginfo_t* info, void* context) {
  void* error_pc = 0;

  const ucontext_t* ucontext = reinterpret_cast<const ucontext_t*>(context);
  if (ucontext != nullptr) {
#ifdef REG_RIP
    // x86_64
    error_pc = reinterpret_cast<void*>(ucontext->uc_mcontext.gregs[REG_RIP]);
#else
#warning "Please enable and test PC retrieval code for your arch in signal_action.cc"
// x86 Classic: reinterpret_cast<void*>(ucontext->uc_mcontext.gregs[REG_EIP]);
// ARM: reinterpret_cast<void*>(ucontext->uc_mcontext.arm_pc);
// PPC: reinterpret_cast<void*>(ucontext->uc_mcontext.regs->nip);
#endif
  }

  BackwardsTrace tracer;
  tracer.logFault(strsignal(sig), info->si_addr);
  if (error_pc != 0) {
    tracer.captureFrom(error_pc);
  } else {
    tracer.capture();
  }
  tracer.logTrace();

  signal(sig, SIG_DFL);
  raise(sig);
}

void SignalAction::installSigHandlers() {
  stack_t stack;
  stack.ss_sp = altstack_ + guard_size_; // Guard page at one end ...
  stack.ss_size = altstack_size_;        // ... guard page at the other
  stack.ss_flags = 0;

  RELEASE_ASSERT(sigaltstack(&stack, nullptr) == 0);

  for (const auto& sig : FATAL_SIGS) {
    struct sigaction saction;
    std::memset(&saction, 0, sizeof(saction));
    sigemptyset(&saction.sa_mask);
    saction.sa_flags = (SA_SIGINFO | SA_ONSTACK | SA_RESETHAND | SA_NODEFER);
    saction.sa_sigaction = sigHandler;
    RELEASE_ASSERT(sigaction(sig, &saction, nullptr) == 0);
  }
}

void SignalAction::removeSigHandlers() const {
  for (const auto& sig : FATAL_SIGS) {
    signal(sig, SIG_DFL);
  }
}

void SignalAction::mapAndProtectStackMemory() {
  // Per docs MAP_STACK doesn't actually do anything today but provides a
  // library hint that might be used in the future.
  altstack_ = static_cast<char*>(mmap(nullptr, mapSizeWithGuards(), PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0));
  RELEASE_ASSERT(altstack_);
  RELEASE_ASSERT(mprotect(altstack_, guard_size_, PROT_NONE) == 0);
  RELEASE_ASSERT(mprotect(altstack_ + guard_size_ + altstack_size_, guard_size_, PROT_NONE) == 0);
}

void SignalAction::unmapStackMemory() {
  munmap(altstack_, mapSizeWithGuards());
}

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
