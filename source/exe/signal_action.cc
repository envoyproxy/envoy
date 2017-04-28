#include "exe/signal_action.h"

#include <signal.h>
#include <sys/mman.h>

#include "common/common/assert.h"

constexpr int SignalAction::FATAL_SIGS[];

void SignalAction::SigHandler(int sig, siginfo_t* info, void* context) {
  void* error_pc = 0;

  const ucontext_t* ucontext = reinterpret_cast<const ucontext_t*>(context);
  if (ucontext != nullptr) {
#ifdef REG_RIP
    // x86_64
    error_pc = reinterpret_cast<void*>(ucontext->uc_mcontext.gregs[REG_RIP]);
#elif defined(REG_EIP)
// x86 Classic - not tested
// error_pc = reinterpret_cast<void*>(ucontext->uc_mcontext.gregs[REG_EIP]);
#warning "Please enable and test x86_32 pc retrieval code in signal_action.cc"
#elif defined(__arm__)
// ARM - not tested
// error_pc = reinterpret_cast<void*>(ucontext->uc_mcontext.arm_pc);
#warning "Please enable and test ARM pc retrieval code in signal_action.cc"
#elif defined(__ppc__)
// PPC - not tested
// error_pc = reinterpret_cast<void*>(ucontext->uc_mcontext.regs->nip);
#warning "Please enable and test PPC pc retrieval code in signal_action.cc"
#else
#warning "Cannot determine PC location in machine context for your architecture"
#endif
  }

  BackwardsTrace tracer(true);
  tracer.LogFault(strsignal(sig), info->si_addr);
  if (error_pc != 0) {
    tracer.CaptureFrom(error_pc);
  } else {
    tracer.Capture();
  }
  tracer.Log();

  signal(sig, SIG_DFL);
  raise(sig);
}

void SignalAction::InstallSigHandlers() {
  stack_t stack;
  stack.ss_sp = altstack_ + GUARD_SIZE; // Guard page at one end ...
  stack.ss_size = ALTSTACK_SIZE;        // ... guard page at the other
  stack.ss_flags = 0;

  if (sigaltstack(&stack, nullptr) < 0) {
    std::cerr << "Failed to set up alternate signal stack: " << strerror(errno) << std::endl;
    RELEASE_ASSERT(false);
  }

  for (const auto& sig : FATAL_SIGS) {
    struct sigaction saction;
    std::memset(&saction, 0, sizeof(saction));
    sigemptyset(&saction.sa_mask);
    saction.sa_flags = (SA_SIGINFO | SA_ONSTACK | SA_RESETHAND | SA_NODEFER);
    saction.sa_sigaction = SigHandler;
    if (sigaction(sig, &saction, nullptr) < 0) {
      std::cerr << "Failed to set up signal action for signal " << sig << ": " << strerror(errno)
                << std::endl;
      RELEASE_ASSERT(false);
    }
  }
}

void SignalAction::RemoveSigHandlers() const {
  for (const auto& sig : FATAL_SIGS) {
    signal(sig, SIG_DFL);
  }
}

void SignalAction::MapAndProtectStackMemory() {
  // Per docs MAP_STACK doesn't actually do anything today but provides a
  // library hint that might be used in the future.
  altstack_ = static_cast<char*>(mmap(nullptr, MapSizeWithGuards(), PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0));
  RELEASE_ASSERT(altstack_);
  if (mprotect(altstack_, GUARD_SIZE, PROT_NONE) < 0) {
    std::cerr << "Failed to protect signal stack memory: " << strerror(errno) << std::endl;
    RELEASE_ASSERT(false);
  }
  if (mprotect(altstack_ + GUARD_SIZE + ALTSTACK_SIZE, GUARD_SIZE, PROT_NONE) < 0) {
    std::cerr << "Failed to protect signal stack memory: " << strerror(errno) << std::endl;
    RELEASE_ASSERT(false);
  }
}

void SignalAction::UnmapStackMemory() {
  if (altstack_ != nullptr) {
    munmap(altstack_, MapSizeWithGuards());
  }
}

void SignalAction::DoGoodAccessForTest() {
  for (size_t i = 0; i < ALTSTACK_SIZE; ++i) {
    *(altstack_ + GUARD_SIZE + i) = 42;
  }
  for (size_t i = 0; i < ALTSTACK_SIZE; ++i) {
    ASSERT(*(altstack_ + GUARD_SIZE + i) == 42);
  }
}

void SignalAction::TryEvilAccessForTest(bool end) {
  if (end) {
    // One byte past the valid region
    // http://oeis.org/A001969
    *(altstack_ + GUARD_SIZE + ALTSTACK_SIZE) = 43;
  } else {
    // One byte before the valid region
    *(altstack_ + GUARD_SIZE - 1) = 43;
  }
}
