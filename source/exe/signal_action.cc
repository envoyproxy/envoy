#include "exe/signal_action.h"

#include <signal.h>

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
    error_pc = reinterpret_cast<void*>(ucontext->uc_mcontext.gregs[REG_EIP]);
#elif defined(__arm__)
    // ARM - not tested
    error_pc = reinterpret_cast<void*>(ucontext->uc_mcontext.arm_pc);
#elif defined(__ppc__)
    // PPC - not tested
    error_pc = reinterpret_cast<void*>(ucontext->uc_mcontext.regs->nip);
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
  stack.ss_sp = altstack_.get();
  stack.ss_size = ALTSTACK_SIZE;
  stack.ss_flags = 0;

  if (sigaltstack(&stack, nullptr) < 0) {
    // Failed sigaltstack.  Just print the error message.
    // We can still try to backtrace, but it might be clobbered.
    std::cerr << "Nonfatal error: Failed to set up alternate signal stack: " << strerror(errno)
              << std::endl;
  }

  for (const auto& sig : FATAL_SIGS) {
    struct sigaction saction;
    std::memset(&saction, 0, sizeof(saction));
    sigemptyset(&saction.sa_mask);
    saction.sa_flags = (SA_SIGINFO | SA_ONSTACK | SA_RESETHAND | SA_NODEFER);
    saction.sa_sigaction = SigHandler;
    if (sigaction(sig, &saction, nullptr) < 0) {
      // Failed sigaltstack.  Just print the error message.
      // Auto backtraces won't work, but we can still start up.
      std::cerr << "Nonfatal error: Failed to set up signal action for signal " << sig << ": "
                << strerror(errno) << std::endl;
    }
  }
}

void SignalAction::RemoveSigHandlers() const {
  for (const auto& sig : FATAL_SIGS) {
    signal(sig, SIG_DFL);
  }
}
